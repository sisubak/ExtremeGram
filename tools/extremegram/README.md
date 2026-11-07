# Avatar marker experiment

This is an offline research tool, not an integrated client feature. It embeds a redundant fixed signature in low-frequency luminance DCT coefficients and detects it by majority vote. It is not a proof of identity: anyone with the algorithm can reproduce it. Its quality depends heavily on image content, clipping, cropping, resampling, and JPEG compression. There is no guarantee that Telegram preserves the mark.

Requirements: Python 3, Pillow and NumPy. Example:

```sh
python -m pip install Pillow numpy
python tools/extremegram/avatar_mark.py encode avatar.png marked.png
python tools/extremegram/avatar_mark.py detect marked.png
```

Local synthetic smoke test, Lanczos resized, JPEG quality 75: a blurred random-color sample matched 64/64 bits at 320 and 160 px, 52/64 at 80 px. A flat black-on-white graphic matched 64/64 at 320 px but only 32/64 at 160 px. These are not Telegram tests or false-positive measurements. Do not automatically change or delete any Telegram avatar based on this experiment.
