// Screen-door transparency: a fragment of something partly shown is kept or
// dropped by a fixed per-pixel pattern, so it stays opaque (depth and
// shadows work as usual) while fading. Interleaved gradient noise (Jimenez,
// "Next Generation Post Processing in Call of Duty: Advanced Warfare",
// SIGGRAPH 2014): even, with no table, alike on every backend

// Drops the fragment at `pixel` (gl_FragCoord.xy) unless `amount` (0..1) of
// it is to be shown. A negative amount keeps -amount from the pattern's other
// side, so what blends in fills exactly what blends out by as much
void DitherDiscard(float amount, vec2 pixel)
{
    if (abs(amount) >= 1.0)
        return;
    float noise = fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
    if (amount < 0.0)
    {
        noise = 1.0 - noise;
        amount = -amount;
    }
    if (amount <= noise)
        discard;
}
