// Run with sips --js. Icon Composer exports edge-to-edge artwork; standalone
// macOS icons use an 824px face centered on a transparent 1024px canvas.
// Canvas also converts the renderer's 16-bit PNG to a compact 8-bit fallback.
const source = sips.images[0];
const size = source.size.width;
const canvas = new Canvas(size, size);
const context = canvas.getContext("2d");
const inset = size * 100 / 1024;
context.drawImage(source, inset, inset, size - 2 * inset, size - 2 * inset);
new Output(context, source.name, "public.png").addToQueue();
