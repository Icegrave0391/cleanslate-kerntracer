import re
fentry_set = set()
non_fentry_set = set()
current = None
has_fentry = False
header_re = re.compile(r'^[0-9a-f]+ <([^>]+)>:$')
with open("kobjdump") as kd:
        for ln in kd:
            line = ln.rstrip()
            m = header_re.match(line)
            if m:
                # flush previous function
                if current is not None:
                    if has_fentry:
                        fentry_set.add(current)
                    else:
                        non_fentry_set.add(current)
                current = m.group(1)
                has_fentry = False
            else:
                if current and '__fentry__' in line:
                    has_fentry = True
        # flush last function
        if current is not None:
            if has_fentry:
                fentry_set.add(current)
            else:
                non_fentry_set.add(current)
import IPython; IPython.embed()