/* Added to the linker's own script for a program kept in a file
 * (tools/build_programs.sh). Espressif's ELF loader (2.1.0) rounds the
 * size of .text up to a whole word when it decides which addresses are
 * code, so read-only data that begins in the last word of .text is given
 * an address in instruction space, where a byte cannot be read: the first
 * string of hello.app was there, and reading it stopped the board
 * (docs/journal/2026-11-17-program-files.md). Read-only data begins on a
 * word of its own. */
SECTIONS
{
    .rodata : ALIGN(4) { *(.rodata .rodata.* .gnu.linkonce.r.*) }
}
INSERT AFTER .text;
