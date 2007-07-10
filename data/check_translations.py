changes={"[record voice]" : "[toggle recording voice]"}

def translation_check(filename, keys):
    print "Examining %s" % (filename)
    f=open(filename)
    lines = [x.replace("\n", "") for x in f.readlines()]
    f.close()
    n = 0
    expect_type="key"
    texts = []
    current_key = ""
    while n<len(lines):
        line = lines[n]
        if expect_type=="key":
            if line == "" or line[0] != '[' or line[-1] != ']':
                print "\tKey on line %i not in correct format, skipping" % (n)
            else:
                current_key = changes.get(line, line)
                expect_type="text"
        elif expect_type == "text":
            texts.append((current_key, line))
            expect_type = "key"
        n += 1
    
    key_set = set(keys)
    local_set = set(n[0] for n in texts)
    for k in key_set.difference(local_set):
        texts.append((k, ""))
    new_texts = [(s[0].lower(), s[0], s[1]) for s in texts]
    new_texts.sort()
    for t in new_texts:
        if t[2] == "":
            print "\tUntranslated key: %s" % (t[1])

    for t in local_set.difference(key_set):
        print "\tKey found not in texts.keys.txt: %s" % (t)
            
    f=open(filename, "w")
    for t in new_texts:
        f.write(t[1]+"\n")
        f.write(t[2]+"\n")
        
        


def main():
    translations=open("texts.list.txt")
    keyf=open("texts.keys.txt")
    keys=[]
    for line in keyf:
        line=line.replace("\n", "")
        keys.append(line)
    keyf.close()
    nkeys = [(s.lower(), s) for s in keys]
    nkeys.sort()
    keyf = open("texts.keys.txt", "w")
    for key in nkeys:
        keyf.write(key[1] + "\n")
    keyf.close()
    for t in translations:
        nt = t.replace("data/", "").replace("\n", "")
        if nt != "texts.keys.txt":
            translation_check(nt, keys) 
    #translation_check("texts.en.txt", keys)

main()
