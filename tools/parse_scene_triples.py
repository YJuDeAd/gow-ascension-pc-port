import re
import sys

def main():
    log_path = 'build/logs/run_20261025_062151_stderr.log'
    if len(sys.argv) > 1:
        log_path = sys.argv[1]

    current_wall = 0.0
    telemetry_re = re.compile(r'\[TELEMETRY 5s\] wall=([\d\.]+)s')
    scene_poll_re = re.compile(r'\[SCENE-POLL #\d+\] state=(\d+) req=0x[0-9A-Fa-f]+ flags=0x[0-9A-Fa-f]+ rem=(\d+) tot=(\d+)')

    triples = []
    last_triple = None

    with open(log_path, 'r', encoding='utf-8', errors='ignore') as f:
        for line_no, line in enumerate(f, 1):
            m_tel = telemetry_re.search(line)
            if m_tel:
                current_wall = float(m_tel.group(1))
                continue
            m_sp = scene_poll_re.search(line)
            if m_sp:
                state = int(m_sp.group(1))
                rem = int(m_sp.group(2))
                tot = int(m_sp.group(3))
                triple = (state, rem, tot)
                if triple != last_triple:
                    triples.append({
                        'triple': triple,
                        'start_line': line_no,
                        'end_line': line_no,
                        'start_wall': current_wall,
                        'end_wall': current_wall
                    })
                    last_triple = triple
                else:
                    triples[-1]['end_line'] = line_no
                    triples[-1]['end_wall'] = current_wall

    print(f"Total distinct consecutive triples: {len(triples)}")
    print(f"{'#':<3} {'State':<6} {'Rem':<10} {'Tot':<10} {'Start Line':<12} {'End Line':<12} {'Wall Time Range'}")
    print("-" * 75)
    for i, t in enumerate(triples):
        s, r, tot = t['triple']
        print(f"{i+1:<3} {s:<6} {r:<10} {tot:<10} {t['start_line']:<12} {t['end_line']:<12} {t['start_wall']:.1f}s .. {t['end_wall']:.1f}s")

if __name__ == '__main__':
    main()
