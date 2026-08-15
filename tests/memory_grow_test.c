/* A memory file that grows keeps what was in it, and one that was never
 * initialised hands out nothing.
 *
 * Virtual memory is one flat block that is enlarged as a run fills it,
 * and everything in it is addressed by an offset from its start. Growing
 * may move that start -- the block is reallocated -- so what was written
 * before a growth has to read back the same afterwards through the same
 * offset. Nothing else in the suite asserts that directly: every run
 * grows its memory and every run would be wrong if a growth lost data,
 * but wrong in whatever way the lost bytes happened to matter, which is
 * a bad way to be told.
 *
 * The other half is the refusal. A memory file with no block is not a
 * memory file, and an allocation asked of one has nowhere to come from;
 * answering an offset into nothing would be a pointer the caller then
 * writes through. So it refuses, and says so on the log -- the message
 * below is expected output, not a fault in the run.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <string.h>

#include "xpost.h"
#include "xpost_memory.h"
#include "xpost_test.h"

int main(void)
{
    static const char pattern[] = "preserve this data across a growth";
    Xpost_Memory_File mem = {0};
    unsigned int addr;

    if (!xpost_init())
    {
        report_failure("the library did not initialise");
        return verdict();
    }

    /* an allocation from a file that was never given a block */
    mem.base = NULL;
    check(xpost_memory_file_alloc(&mem, 64, &addr) == 0,
          "an allocation from a memory file with no block is refused");

    /* and one that keeps what it was given across a growth */
    if (!xpost_memory_file_init(&mem, NULL, -1, NULL, NULL, NULL))
    {
        report_failure("a memory file could not be made");
        xpost_quit();
        return verdict();
    }
    if (!xpost_memory_file_alloc(&mem, sizeof pattern, &addr))
    {
        report_failure("a memory file gave out no block to write in");
        (void)xpost_memory_file_exit(&mem);
        xpost_quit();
        return verdict();
    }
    memcpy((char *)mem.base + addr, pattern, sizeof pattern);

    check(xpost_memory_file_grow(&mem, 4096) == 1, "a memory file grows");
    check(mem.base != NULL, "a grown memory file still has its block");
    check(memcmp((char *)mem.base + addr, pattern, sizeof pattern) == 0,
          "what was written before a growth reads back after it");

    check(xpost_memory_file_exit(&mem) == 1, "a memory file is given back");
    xpost_quit();
    return verdict();
}
