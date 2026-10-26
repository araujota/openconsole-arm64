# OpenConsole GLES ABI 0.1 software reference

The provider is a pre-`ExitBootServices`, cooperative, single-threaded service.
The launcher owns it; clients discover its GUID, create provider-owned contexts,
make one context current, call resolved GLES functions, present, and destroy the
context before returning. There is no TLS or cross-image allocation ownership.

ABI 0.1 accepts only the two shader templates published in
`include/openconsole/gles2.h`, allowing ASCII whitespace and comments. The
renderer string explicitly identifies a non-conformant subset. Targets are
tightly packed BGRA8888, limited to 4096 per dimension and 64 MiB, and must
match the active GOP mode. Presentation is GOP `Blt(BufferToVideo)`.

The one supported draw is `glDrawArrays(GL_TRIANGLES, 0, 3)` with `vec2`
position and `vec3` color client arrays. Positions must lie in clip space. They
are transformed to 24.8 fixed-point window coordinates, sampled at pixel
centers, bounded to the target, and filled by signed edge functions; RGB is
barycentrically interpolated. General clipping is unsupported.

The OVMF image test compares a deterministic 9x9 analytical sample grid,
excluding samples within 0.025 barycentric units of an edge. It requires mean
absolute RGB channel error at most 12 and at least eight chromatic samples.
