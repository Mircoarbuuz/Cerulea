list = []
num = 0
while 1:
    cmd = input("list> ")
    if cmd == "list":
        print(list)
    elif cmd == "append":
        v = input("a: ")
        list.append(v)