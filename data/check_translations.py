def translation_check(filename, keys):
    print "Examining %s" % (filename)
    f=open(filename)
    f.seek(0,2)
    size=f.tell()
    f.seek(0)

    line_n=0
    error_count=0

    file_data=[]
    while f.tell()!=size:
        while f.tell()!=size:
            line_n+=1
            key=f.readline()
            key=key.replace("\n", "")
            if key=="":
                print "  Unexpected empty line in %s at line %i." % (filename, line_n)   
                error_count+=1
            else:
                break
        line_n+=1
        value=f.readline()
        value=value.replace("\n", "")
        if value=="":
             print "  Untranslated key \"%s\" at line %i in %s." % (key, line_n, filename)   
             error_count+=1

        file_data.append((key, value, line_n))

    position=0
    for index, info in enumerate(file_data):
        if position>=len(keys):
            break
        key=info[0]
        value=info[1]
        line_n=info[2]
        if key!=keys[position]:
            print "  Missmatched key at line %i of %s. Expected \"%s\", got \"%s\"" % (line_n, filename, keys[position], key)
            orig_position=position
            try:
                while key!=keys[position]:
                    position+=1
                print "    Key \"%s\" is located %i lines ahead in texts.keys.txt" % (keys[position], position - orig_position)
            except IndexError:
                position=orig_position
                print "    Key \"%s\" not found in texts.keys.txt" % (key)

            error_count+=1
        position+=1

    file_keys=set(x[0] for x in file_data)
    overall_keys=set(keys)
    for key in overall_keys.difference(file_keys):
        print "  Missing key: \"%s\" not found in %s." % (key, filename)

    for key in file_keys.difference(overall_keys):
        print "  Extra key: Key \"%s\" in %s not found in texts.key.txt" % (key, filename)

    if error_count==0:
        print "  File is clear!"
    print ""
    f.close()
    return file_data

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
    

    for line in translations:
        line=line.replace("\n", "")
        if line!="data/texts.keys.txt":
            data = [(s[0].lower(), s[0], s[1]) for s in translation_check(line.replace("data/",""), keys)]
            data.sort()
            f = open(line.replace("data/",""), "w")
            for d in data:
                f.write(d[1]+"\n")
                f.write(d[2]+"\n")

main()
