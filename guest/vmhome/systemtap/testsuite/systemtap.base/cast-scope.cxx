#include "sys/sdt.h"

extern "C" {
#include <string.h>
#include <stdlib.h>
}

class sClass
{
private:
    const char *_str;
    size_t _l;

public:
    sClass(const char *n) : _str(n)
    {
	_l = strlen(_str);
    }

    const char *name(void)
    {
	return _str;
    }

    size_t len(void) const
    {
	return _l;
    }
};

size_t
length(const sClass& str)
{
    int res, r;
    STAP_PROBE1(cast-scope, length, &str);
    r = str.len() * 2;
    STAP_PROBE(cast-scope, dummy); /* Just here to probe line +5. */
    res = r / 2;
    STAP_PROBE(cast-scope, dummy2); /* Just here prevent line reordering. */
    return res;
}

int
main(int argc, char **argv)
{
    if (argc != 2) exit(1);
    sClass hello = sClass(argv[1]);
    return 12 != length(hello);
}
