from add_stuff_base import *

backup("NetMessage.h")
backup("NetMessage.cpp")

print("Name? ")
name = input()

variables = assemble_variables(True, True)
vn = len(variables)


constructor=""
if vn:
    constructor = assemble_constructor_define(variables)

declare_functions = assemble_declare_get_functions(variables)
declare_variables = assemble_declare_variables(variables)
initialize_variable_defaults = assemble_initialize_variable_defaults(variables)
initialize_variables = assemble_initialize_variables(variables)
format_variables = assemble_format_variables(variables)
compare_variables = assemble_compare_variables(variables)
get_function_defines = assemble_get_function_definitions(variables)

hcode = """
///mname
class mname : public NetMessage
{
public:
    ///Creates a mname message
    mname();

"""

if vn:
    hcode+="    ///Creates a mname message\n"
    hcode+="    %s;\n\n" % constructor
hcode+="""  ///Returns Mmname
    Uint8 getMessageType() const;

    ///Encodes the data
    void encodeData(GAGCore::OutputStream* stream) const;

    ///Decodes the data
    void decodeData(GAGCore::InputStream* stream);

    ///Formats the mname message with a small amount
    ///of information.
    std::string format() const;

    ///Compares with another mname
    bool operator==(const NetMessage& rhs) const;
"""
if vn:
    hcode+=declare_functions
    hcode+="private:\n"
    hcode+=declare_variables
hcode+="""};



"""

scode="""
mname::mname()
"""
if vn:
    scode+="    :"
    scode+=initialize_variable_defaults
    scode+="\n"
scode+="""{

}



"""

if vn:
    scode+="mname::%s\n" % constructor
    scode+="    :"
    scode+=initialize_variables
    scode+="\n"
    scode+="{\n}\n\n\n\n"
scode+="""Uint8 mname::getMessageType() const
{
    return Mmname;
}



void mname::encodeData(GAGCore::OutputStream* stream) const
{
    stream->writeEnterSection("mname");
"""
if vn:
    for v in variables:
        scode+="    stream->write%s(%s, \"%s\");\n" % (v[5], v[1], v[1])
scode+="""  stream->writeLeaveSection();
}



void mname::decodeData(GAGCore::InputStream* stream)
{
    stream->readEnterSection("mname");
"""
if vn:
    for v in variables:
        scode+="    %s = stream->read%s(\"%s\");\n" % (v[1], v[5], v[1])
scode+="""  stream->readLeaveSection();
}



std::string mname::format() const
{
""" + format_variables + """
    return s.str();
}



bool mname::operator==(const NetMessage& rhs) const
{
    if(typeid(rhs)==typeid(mname))
    {
""" + compare_variables + """
    }
    return false;
}


"""

scode += get_function_defines


lines = readLines("NetMessage.h")
i = findMarker(lines,"type_append_marker")
lines.insert(i, "   M%s,\n" % name)


i = findMarker(lines,"message_append_marker")
lines.insert(i, hcode.replace("mname", name))
writeLines("NetMessage.h", lines)


lines = readLines("NetMessage.cpp")
i = findMarker(lines, "append_create_point")
lines.insert(i, """     case Mmname:
        message.reset(new mname);
        break;
""".replace("mname", name))

i = findMarker(lines, "append_code_position")
lines.insert(i, scode.replace("mname", name))

writeLines("NetMessage.cpp", lines)

