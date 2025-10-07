print("welcome to FAKEOS")

while True:
    cmd = input("FAKEOS > ")
    if cmd == "":
        continue
    elif cmd == "hello":
        print("Hello, world")
    elif cmd == "exit":
        print("EXITED")
        exit(0)
    else:
        print("COMMAND NOT FOUND")