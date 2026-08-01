import sys

with open("render2d.cpp", "r") as f:
    content = f.read()

insert_idx = content.find("case '.':")
if insert_idx == -1:
    print("Could not find case '.'")
    sys.exit(1)

new_cases = """    case ',': return "00000000000000000000001100011001000";
    case '!': return "00100001000010000100000000010000000";
    case '\\'': return "01000010000100000000000000000000000";
"""

content = content[:insert_idx] + new_cases + content[insert_idx:]

with open("render2d.cpp", "w") as f:
    f.write(content)
