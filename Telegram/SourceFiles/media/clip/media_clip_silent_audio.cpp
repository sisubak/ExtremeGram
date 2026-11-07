/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/clip/media_clip_silent_audio.h"

#include "ffmpeg/ffmpeg_bytes_io_wrap.h"
#include "ffmpeg/ffmpeg_utility.h"
#include "logs.h"

namespace Media::Clip {
namespace {

constexpr auto kAudioFrequency = 48000;
constexpr auto kAudioBitRate = 16 * 1024;

[[nodiscard]] bool WriteAudioFrame(
		not_null<AVFormatContext*> output,
		not_null<AVCodecContext*> codec,
		not_null<AVStream*> stream,
		AVFrame *frame) {
	using namespace FFmpeg;

	auto error = AvErrorWrap(avcodec_send_frame(codec, frame));
	if (error) {
		LogError(u"avcodec_send_frame"_q, error, u"silent audio"_q);
		return false;
	}
	while (true) {
		auto packet = Packet();
		auto &fields = packet.fields();
		error = AvErrorWrap(avcodec_receive_packet(codec, &fields));
		if (error.code() == AVERROR(EAGAIN)
			|| error.code() == AVERROR_EOF) {
			return true;
		} else if (error) {
			LogError(
				u"avcodec_receive_packet"_q,
				error,
				u"silent audio"_q);
			return false;
		}
		fields.stream_index = stream->index;
		av_packet_rescale_ts(
			&fields,
			codec->time_base,
			stream->time_base);
		error = AvErrorWrap(av_interleaved_write_frame(output, &fields));
		if (error) {
			LogError(
				u"av_interleaved_write_frame"_q,
				error,
				u"silent audio"_q);
			return false;
		}
	}
}

[[nodiscard]] bool WriteSilenceUntil(
		int64_t till,
		int64_t &position,
		not_null<AVFormatContext*> output,
		not_null<AVCodecContext*> codec,
		not_null<AVStream*> stream,
		not_null<AVFrame*> frame) {
	using namespace FFmpeg;

	while (position < till) {
		auto error = AvErrorWrap(av_frame_make_writable(frame));
		if (error) {
			LogError(
				u"av_frame_make_writable"_q,
				error,
				u"silent audio"_q);
			return false;
		}
		error = AvErrorWrap(av_samples_set_silence(
			frame->data,
			0,
			frame->nb_samples,
			frame->ch_layout.nb_channels,
			static_cast<AVSampleFormat>(frame->format)));
		if (error) {
			LogError(
				u"av_samples_set_silence"_q,
				error,
				u"silent audio"_q);
			return false;
		}
		frame->pts = position;
		position += frame->nb_samples;
		if (!WriteAudioFrame(output, codec, stream, frame)) {
			return false;
		}
	}
	return true;
}

} // namespace

QByteArray AddSilentAudioTrack(
		const QByteArray &content,
		crl::time duration) {
	using namespace FFmpeg;

	if (content.isEmpty() || duration <= 0) {
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
		LogError(u"avformat_find_stream_info"_q, error, u"silent audio"_q);
		return {};
	}
	const auto videoIndex = av_find_best_stream(
		input.get(),
		AVMEDIA_TYPE_VIDEO,
		-1,
		-1,
		nullptr,
		0);
	if (videoIndex < 0) {
		LogError(
			u"av_find_best_stream"_q,
			AvErrorWrap(videoIndex),
			u"silent audio video"_q);
		return {};
	}
	if (av_find_best_stream(
			input.get(),
			AVMEDIA_TYPE_AUDIO,
			-1,
			-1,
			nullptr,
			0) >= 0) {
		return content;
	}

	auto outputWrap = WriteBytesWrap();
	auto output = MakeWriteFormatPointer(
		static_cast<void*>(&outputWrap),
		nullptr,
		&WriteBytesWrap::Write,
		&WriteBytesWrap::Seek,
		"mp4"_q);
	if (!output) {
		return {};
	}
	const auto inputVideo = input->streams[videoIndex];
	const auto outputVideo = avformat_new_stream(output.get(), nullptr);
	if (!outputVideo) {
		LogError(u"avformat_new_stream"_q, u"silent audio video"_q);
		return {};
	}
	error = AvErrorWrap(avcodec_parameters_copy(
		outputVideo->codecpar,
		inputVideo->codecpar));
	if (error) {
		LogError(
			u"avcodec_parameters_copy"_q,
			error,
			u"silent audio video"_q);
		return {};
	}
	outputVideo->codecpar->codec_tag = 0;
	outputVideo->time_base = inputVideo->time_base;
	outputVideo->avg_frame_rate = inputVideo->avg_frame_rate;
	outputVideo->sample_aspect_ratio = inputVideo->sample_aspect_ratio;
	outputVideo->disposition = inputVideo->disposition;
	av_dict_copy(&outputVideo->metadata, inputVideo->metadata, 0);

	const auto audioEncoder = avcodec_find_encoder(AV_CODEC_ID_AAC);
	if (!audioEncoder) {
		LogError(u"avcodec_find_encoder"_q, u"silent audio AAC"_q);
		return {};
	}
	const auto outputAudio = avformat_new_stream(output.get(), audioEncoder);
	if (!outputAudio) {
		LogError(u"avformat_new_stream"_q, u"silent audio AAC"_q);
		return {};
	}
	auto audioCodec = CodecPointer(avcodec_alloc_context3(audioEncoder));
	if (!audioCodec) {
		LogError(u"avcodec_alloc_context3"_q, u"silent audio AAC"_q);
		return {};
	}
	audioCodec->codec_type = AVMEDIA_TYPE_AUDIO;
	audioCodec->codec_id = AV_CODEC_ID_AAC;
	audioCodec->sample_fmt = AV_SAMPLE_FMT_FLTP;
	audioCodec->bit_rate = kAudioBitRate;
	audioCodec->sample_rate = kAudioFrequency;
	audioCodec->ch_layout = AV_CHANNEL_LAYOUT_MONO;
	audioCodec->time_base = AVRational{ 1, kAudioFrequency };
	if (output->oformat->flags & AVFMT_GLOBALHEADER) {
		audioCodec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
	}
	error = AvErrorWrap(avcodec_open2(
		audioCodec.get(),
		audioEncoder,
		nullptr));
	if (error) {
		LogError(u"avcodec_open2"_q, error, u"silent audio AAC"_q);
		return {};
	}
	error = AvErrorWrap(avcodec_parameters_from_context(
		outputAudio->codecpar,
		audioCodec.get()));
	if (error) {
		LogError(
			u"avcodec_parameters_from_context"_q,
			error,
			u"silent audio AAC"_q);
		return {};
	}
	outputAudio->time_base = audioCodec->time_base;

	auto audioFrame = MakeFramePointer();
	if (!audioFrame || audioCodec->frame_size <= 0) {
		LogError(u"av_frame_alloc"_q, u"silent audio AAC"_q);
		return {};
	}
	audioFrame->nb_samples = audioCodec->frame_size;
	audioFrame->format = audioCodec->sample_fmt;
	audioFrame->sample_rate = audioCodec->sample_rate;
	av_channel_layout_copy(
		&audioFrame->ch_layout,
		&audioCodec->ch_layout);
	error = AvErrorWrap(av_frame_get_buffer(audioFrame.get(), 0));
	if (error) {
		LogError(u"av_frame_get_buffer"_q, error, u"silent audio AAC"_q);
		return {};
	}

	auto options = (AVDictionary*)nullptr;
	const auto optionsGuard = gsl::finally([&] {
		av_dict_free(&options);
	});
	av_dict_set(
		&options,
		"movflags",
		"empty_moov+frag_keyframe+default_base_moof",
		0);
	error = AvErrorWrap(avformat_write_header(output.get(), &options));
	if (error) {
		LogError(u"avformat_write_header"_q, error, u"silent audio"_q);
		return {};
	}

	const auto totalAudioSamples = av_rescale_rnd(
		duration,
		kAudioFrequency,
		1000,
		AV_ROUND_UP);
	auto audioPosition = int64_t(0);
	auto packet = AVPacket();
	while (true) {
		error = AvErrorWrap(av_read_frame(input.get(), &packet));
		if (error.code() == AVERROR_EOF) {
			break;
		} else if (error) {
			LogError(u"av_read_frame"_q, error, u"silent audio"_q);
			return {};
		}
		const auto packetGuard = gsl::finally([&] {
			av_packet_unref(&packet);
		});
		if (packet.stream_index != videoIndex) {
			continue;
		}
		const auto timestamp = (packet.dts != AV_NOPTS_VALUE)
			? packet.dts
			: packet.pts;
		if (timestamp != AV_NOPTS_VALUE) {
			const auto packetEnd = timestamp
				+ std::max(int64_t(packet.duration), int64_t());
			const auto audioTill = std::clamp(
				av_rescale_q(
					packetEnd,
					inputVideo->time_base,
					audioCodec->time_base),
				int64_t(),
				totalAudioSamples);
			if (!WriteSilenceUntil(
					audioTill,
					audioPosition,
					output.get(),
					audioCodec.get(),
					outputAudio,
					audioFrame.get())) {
				return {};
			}
		}
		packet.stream_index = outputVideo->index;
		packet.pos = -1;
		av_packet_rescale_ts(
			&packet,
			inputVideo->time_base,
			outputVideo->time_base);
		error = AvErrorWrap(av_interleaved_write_frame(
			output.get(),
			&packet));
		if (error) {
			LogError(
				u"av_interleaved_write_frame"_q,
				error,
				u"silent audio video"_q);
			return {};
		}
	}
	if (!WriteSilenceUntil(
			totalAudioSamples,
			audioPosition,
			output.get(),
			audioCodec.get(),
			outputAudio,
			audioFrame.get())
		|| !WriteAudioFrame(
			output.get(),
			audioCodec.get(),
			outputAudio,
			nullptr)) {
		return {};
	}
	error = AvErrorWrap(av_write_trailer(output.get()));
	if (error) {
		LogError(u"av_write_trailer"_q, error, u"silent audio"_q);
		return {};
	}
	return std::move(outputWrap.content);
}

} // namespace Media::Clip
