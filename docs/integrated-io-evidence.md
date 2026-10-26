# Integrated OVMF I/O evidence

`make test-integrated-io-ovmf` launches the same independent child twice, once
with a normal return and once with an error return. While each child is active it
performs all of the following cooperatively before `ExitBootServices`:

- presents an animated shader-backed triangle through `OC_GLES2_PROTOCOL`;
- pumps centered 330-Hz music and a keyboard-triggered, right-panned 990-Hz effect;
- obtains DHCP and completes a bounded UDP fixture exchange;
- accepts keyboard input while rendering, networking, and audio continue.

The harness captures two separated GOP frames and requires different hashes,
observes packets outside the VM, analyzes the WAV rather than trusting markers,
and checks both parent cleanup paths. `build/ovmf-integrated-io-metrics.json`
records two launches, GLES frame counts, DHCP and UDP latency, voice underruns,
and output latency. The VM budgets are 15 seconds for DHCP, 5 seconds for UDP,
zero underruns, at least 16 presented frames, and at most 12,288 latency frames.
The latency ceiling represents AudioIo's three rotating 4096-frame buffers at
48 kHz (256 ms); it is VM backend evidence, not a physical latency claim.

The companion `build/ovmf-integrated-audio-metrics.json` records the externally
captured PCM format, duration, activity, and peaks. Successful normal and error
returns must each leave zero live OpenConsole network allocations, no owned
voices, and no pending AudioIo buffers. This evidence satisfies task 8.1 only;
it does not substitute for physical RTL8125 or analog HDA proof.
