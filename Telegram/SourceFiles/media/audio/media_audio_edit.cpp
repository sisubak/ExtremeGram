/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/audio/media_audio_edit.h"

#include "ffmpeg/ffmpeg_bytes_io_wrap.h"
#include "ffmpeg/ffmpeg_utility.h"

#include <QtCore/QFile>
#include <cstring>
#include <vector>

namespace Media {

[[nodiscard]] AudioEditResult TrimAudioToRange(
		const QByteArray &content,
		crl::time from,
		crl::time till) {
	using namespace FFmpeg;

	if (content.isEmpty() || (from < 0) || (till <= from)) {
		return {};
	}

	auto inputWrap = ReadBytesWrap{
		.size = content.size(),
		.data = reinterpret_cast<const uchar*>(content.constData()),
	};
	auto input = MakeFormatPointer(
		&inputWrap,
		&ReadBytesWrap::Read,
		nullptr,
		&ReadBytesWrap::Seek);
	if (!input) {
		return {};
	}

	auto error = AvErrorWrap(avformat_find_stream_info(input.get(), nullptr));
	if (error) {
		LogError(u"avformat_find_stream_info"_q, error);
		return {};
	}

	const auto streamId = av_find_best_stream(
		input.get(),
		AVMEDIA_TYPE_AUDIO,
		-1,
		-1,
		nullptr,
		0);
	if (streamId < 0) {
		LogError(u"av_find_best_stream"_q, AvErrorWrap(streamId));
		return {};
	}

	const auto inStream = input->streams[streamId];
	auto outputWrap = WriteBytesWrap();
	auto output = MakeWriteFormatPointer(
		static_cast<void*>(&outputWrap),
		nullptr,
		&WriteBytesWrap::Write,
		&WriteBytesWrap::Seek,
		"opus"_q);
	if (!output) {
		return {};
	}

	const auto outStream = avformat_new_stream(output.get(), nullptr);
	if (!outStream) {
		LogError(u"avformat_new_stream"_q);
		return {};
	}

	error = AvErrorWrap(avcodec_parameters_copy(
		outStream->codecpar,
		inStream->codecpar));
	if (error) {
		LogError(u"avcodec_parameters_copy"_q, error);
		return {};
	}
	outStream->codecpar->codec_tag = 0;
	outStream->time_base = inStream->time_base;

	error = AvErrorWrap(avformat_write_header(output.get(), nullptr));
	if (error) {
		LogError(u"avformat_write_header"_q, error);
		return {};
	}

	const auto fromPts = TimeToPts(from, inStream->time_base);
	const auto tillPts = TimeToPts(till, inStream->time_base);
	auto firstPts = int64(AV_NOPTS_VALUE);
	auto firstDts = int64(AV_NOPTS_VALUE);
	auto lastPts = std::numeric_limits<int64>::min();
	auto lastDts = std::numeric_limits<int64>::min();
	auto durationPts = int64(0);
	auto copied = 0;

	auto packet = AVPacket();
	av_init_packet(&packet);
	while (true) {
		error = AvErrorWrap(av_read_frame(input.get(), &packet));
		if (error.code() == AVERROR_EOF) {
			break;
		} else if (error) {
			LogError(u"av_read_frame"_q, error);
			return {};
		}
		const auto guard = gsl::finally([&] {
			av_packet_unref(&packet);
		});
		if (packet.stream_index != streamId) {
			continue;
		}

		const auto packetStart = (packet.pts != AV_NOPTS_VALUE)
			? packet.pts
			: packet.dts;
		const auto packetDuration = std::max(int64(packet.duration), int64());
		const auto packetEnd = (packetStart != AV_NOPTS_VALUE)
			? (packetStart + packetDuration)
			: AV_NOPTS_VALUE;
		if ((packetStart != AV_NOPTS_VALUE) && (packetStart >= tillPts)) {
			break;
		}
		if ((packetEnd != AV_NOPTS_VALUE) && (packetEnd <= fromPts)) {
			continue;
		}

		if (packet.pts != AV_NOPTS_VALUE) {
			if (firstPts == AV_NOPTS_VALUE) {
				firstPts = packet.pts;
			}
			packet.pts -= firstPts;
			if (packet.pts < 0) {
				packet.pts = 0;
			}
			if (packet.pts <= lastPts) {
				packet.pts = lastPts + 1;
			}
			lastPts = packet.pts;
		}
		if (packet.dts != AV_NOPTS_VALUE) {
			if (firstDts == AV_NOPTS_VALUE) {
				firstDts = packet.dts;
			}
			packet.dts -= firstDts;
			if (packet.dts < 0) {
				packet.dts = 0;
			}
			if (packet.dts <= lastDts) {
				packet.dts = lastDts + 1;
			}
			lastDts = packet.dts;
		}

		const auto packetPosition = (packet.pts != AV_NOPTS_VALUE)
			? packet.pts
			: packet.dts;
		if (packetPosition != AV_NOPTS_VALUE) {
			durationPts = std::max(
				durationPts,
				packetPosition + std::max(int64(packet.duration), int64()));
		}

		packet.stream_index = outStream->index;
		error = AvErrorWrap(av_interleaved_write_frame(output.get(), &packet));
		if (error) {
			LogError(u"av_interleaved_write_frame"_q, error);
			return {};
		}
		++copied;
	}

	if (!copied) {
		return {};
	}

	error = AvErrorWrap(av_write_trailer(output.get()));
	if (error) {
		LogError(u"av_write_trailer"_q, error);
		return {};
	}

	auto result = AudioEditResult();
	result.content = std::move(outputWrap.content);
	result.waveform = audioCountWaveform(Core::FileLocation(), result.content);
	result.duration = durationPts
		? PtsToTimeCeil(durationPts, outStream->time_base)
		: (till - from);
	return result;
}

[[nodiscard]] AudioEditResult ConcatAudio(
		const QByteArray &first,
		const QByteArray &second) {
	using namespace FFmpeg;

	if (first.isEmpty() || second.isEmpty()) {
		return {};
	}

	auto firstWrap = ReadBytesWrap{
		.size = first.size(),
		.data = reinterpret_cast<const uchar*>(first.constData()),
	};
	auto firstInput = MakeFormatPointer(
		&firstWrap,
		&ReadBytesWrap::Read,
		nullptr,
		&ReadBytesWrap::Seek);
	if (!firstInput) {
		return {};
	}

	auto secondWrap = ReadBytesWrap{
		.size = second.size(),
		.data = reinterpret_cast<const uchar*>(second.constData()),
	};
	auto secondInput = MakeFormatPointer(
		&secondWrap,
		&ReadBytesWrap::Read,
		nullptr,
		&ReadBytesWrap::Seek);
	if (!secondInput) {
		return {};
	}

	const auto prepareStream = [](not_null<AVFormatContext*> input) {
		auto error = AvErrorWrap(avformat_find_stream_info(input, nullptr));
		if (error) {
			LogError(u"avformat_find_stream_info"_q, error);
			return static_cast<AVStream*>(nullptr);
		}
		const auto streamId = av_find_best_stream(
			input,
			AVMEDIA_TYPE_AUDIO,
			-1,
			-1,
			nullptr,
			0);
		if (streamId < 0) {
			LogError(u"av_find_best_stream"_q, AvErrorWrap(streamId));
			return static_cast<AVStream*>(nullptr);
		}
		return input->streams[streamId];
	};
	const auto firstStream = prepareStream(firstInput.get());
	if (!firstStream) {
		return {};
	}
	const auto secondStream = prepareStream(secondInput.get());
	if (!secondStream) {
		return {};
	}

	auto outputWrap = WriteBytesWrap();
	auto output = MakeWriteFormatPointer(
		static_cast<void*>(&outputWrap),
		nullptr,
		&WriteBytesWrap::Write,
		&WriteBytesWrap::Seek,
		"opus"_q);
	if (!output) {
		return {};
	}
	const auto outStream = avformat_new_stream(output.get(), nullptr);
	if (!outStream) {
		LogError(u"avformat_new_stream"_q);
		return {};
	}

	auto error = AvErrorWrap(avcodec_parameters_copy(
		outStream->codecpar,
		firstStream->codecpar));
	if (error) {
		LogError(u"avcodec_parameters_copy"_q, error);
		return {};
	}
	outStream->codecpar->codec_tag = 0;
	outStream->time_base = firstStream->time_base;

	error = AvErrorWrap(avformat_write_header(output.get(), nullptr));
	if (error) {
		LogError(u"avformat_write_header"_q, error);
		return {};
	}

	auto offsetPts = int64(0);
	auto durationPts = int64(0);
	auto lastPts = std::numeric_limits<int64>::min();
	auto lastDts = std::numeric_limits<int64>::min();
	auto copied = 0;

	const auto append = [&](
			not_null<AVFormatContext*> input,
			not_null<AVStream*> inStream) {
		auto firstPts = int64(AV_NOPTS_VALUE);
		auto firstDts = int64(AV_NOPTS_VALUE);
		auto sourceEndPts = offsetPts;

		auto packet = AVPacket();
		av_init_packet(&packet);
		while (true) {
			auto error = AvErrorWrap(av_read_frame(input, &packet));
			if (error.code() == AVERROR_EOF) {
				break;
			} else if (error) {
				LogError(u"av_read_frame"_q, error);
				return false;
			}
			const auto guard = gsl::finally([&] {
				av_packet_unref(&packet);
			});
			if (packet.stream_index != inStream->index) {
				continue;
			}

			if (packet.pts != AV_NOPTS_VALUE) {
				if (firstPts == AV_NOPTS_VALUE) {
					firstPts = packet.pts;
				}
				packet.pts -= firstPts;
				if (packet.pts < 0) {
					packet.pts = 0;
				}
				packet.pts = av_rescale_q(
					packet.pts,
					inStream->time_base,
					outStream->time_base);
				packet.pts += offsetPts;
				if (packet.pts <= lastPts) {
					packet.pts = lastPts + 1;
				}
				lastPts = packet.pts;
			}
			if (packet.dts != AV_NOPTS_VALUE) {
				if (firstDts == AV_NOPTS_VALUE) {
					firstDts = packet.dts;
				}
				packet.dts -= firstDts;
				if (packet.dts < 0) {
					packet.dts = 0;
				}
				packet.dts = av_rescale_q(
					packet.dts,
					inStream->time_base,
					outStream->time_base);
				packet.dts += offsetPts;
				if (packet.dts <= lastDts) {
					packet.dts = lastDts + 1;
				}
				lastDts = packet.dts;
			}
			packet.duration = (packet.duration > 0)
				? av_rescale_q(
					packet.duration,
					inStream->time_base,
					outStream->time_base)
				: 0;

			const auto packetPosition = (packet.pts != AV_NOPTS_VALUE)
				? packet.pts
				: packet.dts;
			if (packetPosition != AV_NOPTS_VALUE) {
				const auto packetEnd = packetPosition
					+ std::max(int64(packet.duration), int64());
				durationPts = std::max(durationPts, packetEnd);
				sourceEndPts = std::max(sourceEndPts, packetEnd);
			}

			packet.stream_index = outStream->index;
			error = AvErrorWrap(av_interleaved_write_frame(output.get(), &packet));
			if (error) {
				LogError(u"av_interleaved_write_frame"_q, error);
				return false;
			}
			++copied;
		}

		offsetPts = sourceEndPts;
		return true;
	};
	if (!append(firstInput.get(), firstStream)
		|| !append(secondInput.get(), secondStream)) {
		return {};
	}
	if (!copied) {
		return {};
	}

	error = AvErrorWrap(av_write_trailer(output.get()));
	if (error) {
		LogError(u"av_write_trailer"_q, error);
		return {};
	}

	auto result = AudioEditResult();
	result.content = std::move(outputWrap.content);
	result.waveform = audioCountWaveform(Core::FileLocation(), result.content);
	result.duration = durationPts
		? PtsToTimeCeil(durationPts, outStream->time_base)
		: 0;
	return result;
}

[[nodiscard]] AudioEditResult ConvertFileToVoice(const QString &filepath) {
	using namespace FFmpeg;

	if (filepath.isEmpty()) {
		return {};
	}
	QFile file(filepath);
	if (!file.open(QIODevice::ReadOnly) || file.size() <= 0) {
		return {};
	}

	struct FileReadWrap {
		QFile *file = nullptr;

		static int Read(void *opaque, uint8_t *buf, int bufSize) {
			const auto self = static_cast<FileReadWrap*>(opaque);
			if (!self || !self->file || bufSize <= 0) {
				return AVERROR_EOF;
			}
			const auto n = self->file->read(
				reinterpret_cast<char*>(buf),
				bufSize);
			return (n > 0) ? int(n) : AVERROR_EOF;
		}

		static int64_t Seek(void *opaque, int64_t offset, int whence) {
			const auto self = static_cast<FileReadWrap*>(opaque);
			if (!self || !self->file) {
				return -1;
			}
			if (whence & AVSEEK_SIZE) {
				return self->file->size();
			}
			const auto base = (whence & ~AVSEEK_FORCE);
			int64_t target = 0;
			if (base == SEEK_SET) {
				target = offset;
			} else if (base == SEEK_CUR) {
				target = self->file->pos() + offset;
			} else if (base == SEEK_END) {
				target = self->file->size() + offset;
			} else {
				return -1;
			}
			if (target < 0 || !self->file->seek(target)) {
				return -1;
			}
			return self->file->pos();
		}
	};

	auto readWrap = FileReadWrap{ .file = &file };
	auto input = MakeFormatPointer(
		&readWrap,
		&FileReadWrap::Read,
		nullptr,
		&FileReadWrap::Seek);
	if (!input) {
		return {};
	}

	auto error = AvErrorWrap(avformat_find_stream_info(input.get(), nullptr));
	if (error) {
		LogError(u"avformat_find_stream_info"_q, error);
		return {};
	}

	const auto streamId = av_find_best_stream(
		input.get(),
		AVMEDIA_TYPE_AUDIO,
		-1,
		-1,
		nullptr,
		0);
	if (streamId < 0) {
		LogError(u"av_find_best_stream"_q, AvErrorWrap(streamId));
		return {};
	}

	const auto inStream = input->streams[streamId];
	auto decCtx = MakeCodecPointer({ .stream = inStream });
	if (!decCtx) {
		return {};
	}

	auto outputWrap = WriteBytesWrap();
	auto output = MakeWriteFormatPointer(
		static_cast<void*>(&outputWrap),
		nullptr,
		&WriteBytesWrap::Write,
		&WriteBytesWrap::Seek,
		"opus"_q);
	if (!output || !output->oformat) {
		return {};
	}

	const auto *encCodec = avcodec_find_encoder(output->oformat->audio_codec);
	if (!encCodec) {
		encCodec = avcodec_find_encoder(AV_CODEC_ID_OPUS);
	}
	if (!encCodec) {
		return {};
	}

	const auto outStream = avformat_new_stream(output.get(), encCodec);
	if (!outStream) {
		return {};
	}

	auto encCtx = CodecPointer(avcodec_alloc_context3(encCodec));
	if (!encCtx) {
		return {};
	}

	constexpr auto kTargetRate = 48000;
	encCtx->sample_fmt = AV_SAMPLE_FMT_FLTP;
	encCtx->bit_rate = 48000;
	encCtx->ch_layout = AV_CHANNEL_LAYOUT_MONO;
	encCtx->sample_rate = kTargetRate;
	encCtx->time_base = AVRational{ 1, kTargetRate };
	if (output->oformat->flags & AVFMT_GLOBALHEADER) {
		encCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
	}

	error = AvErrorWrap(avcodec_open2(encCtx.get(), encCodec, nullptr));
	if (error) {
		LogError(u"avcodec_open2"_q, error);
		return {};
	}

	error = AvErrorWrap(avcodec_parameters_from_context(
		outStream->codecpar,
		encCtx.get()));
	if (error) {
		LogError(u"avcodec_parameters_from_context"_q, error);
		return {};
	}
	outStream->codecpar->codec_tag = 0;
	outStream->time_base = encCtx->time_base;

	error = AvErrorWrap(avformat_write_header(output.get(), nullptr));
	if (error) {
		LogError(u"avformat_write_header"_q, error);
		return {};
	}

	const int frameSize = (encCtx->frame_size > 0) ? encCtx->frame_size : 960;
	auto decFrame = MakeFramePointer();
	auto encFrame = MakeFramePointer();
	if (!decFrame || !encFrame) {
		return {};
	}
	encFrame->format = encCtx->sample_fmt;
	encFrame->ch_layout = AV_CHANNEL_LAYOUT_MONO;
	encFrame->sample_rate = kTargetRate;
	encFrame->nb_samples = frameSize;
	if (av_frame_get_buffer(encFrame.get(), 0) < 0) {
		return {};
	}

	SwresamplePointer swr;
	AVChannelLayout dstLayout = AV_CHANNEL_LAYOUT_MONO;
	std::vector<float> swrBuf;
	std::vector<float> pcmQueue;
	size_t pcmReadPos = 0;
	int64_t totalEncodedSamples = 0;
	int64_t nextPts = 0;

	const auto drainEncoder = [&]() -> bool {
		auto pkt = Packet();
		while (true) {
			const auto r = avcodec_receive_packet(encCtx.get(), &pkt.fields());
			if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) {
				return true;
			} else if (r < 0) {
				return false;
			}
			av_packet_rescale_ts(
				&pkt.fields(),
				encCtx->time_base,
				outStream->time_base);
			pkt.fields().stream_index = outStream->index;
			const auto w = av_interleaved_write_frame(
				output.get(),
				&pkt.fields());
			av_packet_unref(&pkt.fields());
			if (w < 0) {
				return false;
			}
		}
	};

	const auto encodeAvailable = [&](bool flushTail) -> bool {
		while (true) {
			const auto avail = pcmQueue.size() - pcmReadPos;
			if (avail == 0 || (!flushTail && avail < size_t(frameSize))) {
				break;
			}
			const auto chunk = int(std::min(avail, size_t(frameSize)));
			if (av_frame_make_writable(encFrame.get()) < 0) {
				return false;
			}
			encFrame->nb_samples = chunk;
			std::memcpy(
				encFrame->data[0],
				pcmQueue.data() + pcmReadPos,
				size_t(chunk) * sizeof(float));
			pcmReadPos += size_t(chunk);
			encFrame->pts = nextPts;
			nextPts += chunk;
			totalEncodedSamples += chunk;
			if (avcodec_send_frame(encCtx.get(), encFrame.get()) < 0) {
				return false;
			}
			if (!drainEncoder()) {
				return false;
			}
		}
		if (pcmReadPos > 65536) {
			pcmQueue.erase(
				pcmQueue.begin(),
				pcmQueue.begin() + ptrdiff_t(pcmReadPos));
			pcmReadPos = 0;
		}
		return true;
	};

	const auto pushDecodedFrame = [&](not_null<AVFrame*> frame) -> bool {
		if (frame->nb_samples <= 0) {
			return true;
		}
		const auto srcRate = (frame->sample_rate > 0)
			? frame->sample_rate
			: ((decCtx->sample_rate > 0) ? decCtx->sample_rate : kTargetRate);
		const auto srcFmt = static_cast<AVSampleFormat>(frame->format);
		AVChannelLayout srcLayout = {};
		if (frame->ch_layout.nb_channels > 0) {
			av_channel_layout_copy(&srcLayout, &frame->ch_layout);
		} else if (decCtx->ch_layout.nb_channels > 0) {
			av_channel_layout_copy(&srcLayout, &decCtx->ch_layout);
		} else {
			av_channel_layout_default(&srcLayout, 1);
		}
		const auto layoutGuard = gsl::finally([&] {
			av_channel_layout_uninit(&srcLayout);
		});

		swr = MakeSwresamplePointer(
			&srcLayout,
			srcFmt,
			srcRate,
			&dstLayout,
			AV_SAMPLE_FMT_FLTP,
			kTargetRate,
			&swr);
		if (!swr) {
			return false;
		}

		const auto delay = swr_get_delay(swr.get(), srcRate);
		const auto maxOut = int(av_rescale_rnd(
			delay + frame->nb_samples,
			kTargetRate,
			srcRate,
			AV_ROUND_UP)) + 32;
		if (int(swrBuf.size()) < maxOut) {
			swrBuf.resize(size_t(maxOut));
		}
		uint8_t *outPlanes[1] = {
			reinterpret_cast<uint8_t*>(swrBuf.data()),
		};
		const auto converted = swr_convert(
			swr.get(),
			outPlanes,
			maxOut,
			const_cast<const uint8_t**>(frame->extended_data),
			frame->nb_samples);
		if (converted < 0) {
			return false;
		}
		if (converted > 0) {
			pcmQueue.insert(
				pcmQueue.end(),
				swrBuf.begin(),
				swrBuf.begin() + converted);
		}
		return encodeAvailable(false);
	};

	auto inPkt = Packet();
	while (true) {
		const auto r = av_read_frame(input.get(), &inPkt.fields());
		if (r == AVERROR_EOF) {
			break;
		} else if (r < 0) {
			av_packet_unref(&inPkt.fields());
			break;
		}
		const auto unrefGuard = gsl::finally([&] {
			av_packet_unref(&inPkt.fields());
		});
		if (inPkt.fields().stream_index != streamId) {
			continue;
		}
		if (avcodec_send_packet(decCtx.get(), &inPkt.fields()) < 0) {
			continue;
		}
		while (avcodec_receive_frame(decCtx.get(), decFrame.get()) == 0) {
			const auto ok = pushDecodedFrame(decFrame.get());
			av_frame_unref(decFrame.get());
			if (!ok) {
				return {};
			}
		}
	}

	avcodec_send_packet(decCtx.get(), nullptr);
	while (avcodec_receive_frame(decCtx.get(), decFrame.get()) == 0) {
		const auto ok = pushDecodedFrame(decFrame.get());
		av_frame_unref(decFrame.get());
		if (!ok) {
			return {};
		}
	}

	if (swr) {
		if (swrBuf.size() < 4096) {
			swrBuf.resize(4096);
		}
		while (true) {
			uint8_t *outPlanes[1] = {
				reinterpret_cast<uint8_t*>(swrBuf.data()),
			};
			const auto flushed = swr_convert(
				swr.get(),
				outPlanes,
				int(swrBuf.size()),
				nullptr,
				0);
			if (flushed <= 0) {
				break;
			}
			pcmQueue.insert(
				pcmQueue.end(),
				swrBuf.begin(),
				swrBuf.begin() + flushed);
		}
	}

	if (!encodeAvailable(true)) {
		return {};
	}
	if (totalEncodedSamples <= 0) {
		return {};
	}

	avcodec_send_frame(encCtx.get(), nullptr);
	if (!drainEncoder()) {
		return {};
	}

	error = AvErrorWrap(av_write_trailer(output.get()));
	if (error) {
		LogError(u"av_write_trailer"_q, error);
		return {};
	}

	auto result = AudioEditResult();
	result.content = std::move(outputWrap.content);
	result.waveform = audioCountWaveform(Core::FileLocation(), result.content);
	result.duration = std::max(
		crl::time(1),
		crl::time((totalEncodedSamples * 1000LL + kTargetRate - 1)
			/ kTargetRate));
	return result;
}

} // namespace Media
