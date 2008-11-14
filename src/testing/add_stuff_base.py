def getVariable(name):
	print "%s?" % name
	return raw_input()

def readLines(file):
	f = open(file, 'r')
	lines = f.readlines()
	f.close()
	return lines

def writeLines(file, lines):
	f = open(file, 'w')
	lines = f.writelines(lines)
	f.close()

def findMarker(lines, marker):
	for index,line in enumerate(lines):
		if line.find(marker) != -1:
			return index

def backup(file):
	f = open(file, 'r')
	lines=f.readlines()
	f.close()
	f = open(file+".txt",'w')
	f.writelines(lines)
	f.close()
	
def assemble_variables(need_default=False, need_write=False):
	vn=0
	variables=[]
	while True:
		set = []
		print "Type %i?" % (vn+1)
		set.append(raw_input())
		if not set[0]:
			break
		else:
			print "Name %i?" % (vn+1)
			set.append(raw_input())
			print "get name %i?" % (vn+1)
			set.append(raw_input())
			print "format %i" % (vn+1)
			set.append(raw_input())
			if need_default:
				print "default %i" % (vn+1)
				set.append(raw_input())
			if need_write:
				print "write %i" % (vn+1)
				set.append(raw_input())
			variables.append(tuple(set))
			vn+=1
	return variables

def assemble_constructor_define(variables):
	constructor="mname("
	for v in variables:
		if v==variables[-1]:
			constructor+="%s %s" % (v[0], v[1])
		else:
			constructor+="%s %s, " % (v[0], v[1])
	constructor+=")"
	return constructor

def assemble_declare_get_functions(variables):
	declare_functions=""
	for v in variables:
		declare_functions+="\n"
		declare_functions+="	///Retrieves %s\n" % v[1]
		declare_functions+="	%s %s() const;\n" % (v[0], v[2])
	return declare_functions

def assemble_declare_variables(variables):
	declare_variables=""
	if variables:
		declare_variables+="private:\n"
		for v in variables:
			declare_variables+="	%s %s;\n" % (v[0], v[1])
	return declare_variables

def assemble_initialize_variables(variables):
	initialize_variables=""
	for v in variables:
		initialize_variables+="%s(%s)" % (v[1], v[1])
		if v!=variables[-1]:
			initialize_variables+=", "
	return initialize_variables

def assemble_initialize_variable_defaults(variables):
	initialize_variables=""
	for v in variables:
		initialize_variables+=" %s(%s)" % (v[1], v[4])
		if v!=variables[-1]:
			initialize_variables+=","
	return initialize_variables
	
def assemble_format_variables(variables):
	format = "	std::ostringstream s;\n"
	format+= "	s<<\"mname("
	if variables:
		format+="\""
		for v in variables:
			format+="<<\"%s=\"<<%s<<\"; \"" % (v[3], v[3])
		format+="<<\""
	format+=")\";"
	return format
	
def assemble_compare_variables(variables):
	scode = ""
	if variables:
		scode+="		const mname& r = dynamic_cast<const mname&>(rhs);\n"
		scode+="		if("
		for v in variables:
			scode+="r.%s == %s" % (v[1], v[1])
			if v!=variables[-1]:
				scode+=" && "
		scode+=")\n"
		scode+="			return true;"
	else:
		scode+="		//const mname& r = dynamic_cast<const mname&>(rhs);\n"
		scode+="		return true;"
	return scode


def assemble_get_function_definitions(variables):
	scode = ""
	for v in variables:
		scode+="%s mname::%s() const\n" % (v[0], v[2])
		scode+="{\n"
		scode+="	return %s;\n" % (v[1])
		scode+="}\n\n\n\n"
	return scode
