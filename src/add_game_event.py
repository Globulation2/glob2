from add_stuff_base import *

backup("GameEvent.h")
backup("GameEvent.cpp")

name = getVariable("name")
lines = readLines("GameEvent.h")
i = findMarker(lines,"type_append_marker")
lines.insert(i, "	GE%s,\n" % name.replace("Event", ""))


i = findMarker(lines,"event_append_marker")
lines.insert(i, """
class mname : public GameEvent
{
public:
	///Constructs a mname event
	mname(Uint32 step, Sint16 x, Sint16 y, Uint8 type);

	///This formats a user-readable message, including translating the message
	std::string formatMessage();

	///Returns the color of the message after its formatted
	GAGCore::Color formatColor();

	///Returns the event type
	Uint8 getEventType();
private:
	Uint8 type;
};



""".replace("mname", name))
writeLines("GameEvent.h", lines)


lines = readLines("GameEvent.cpp")
i = findMarker(lines, "code_append_marker")
lines.insert(i, """
mnameEvent::mname(Uint32 step, Sint16 x, Sint16 y, Uint8 type)
	: GameEvent(step, x, y), type(type)
{

}



std::string mname::formatMessage()
{
	std::string message;
	//message += FormatableString(Toolkit::getStringTable("")->getString(""));
	return message;
}



GAGCore::Color mname::formatColor()
{
	return GAGCore::Color(0, 0, 0);
}



Uint8 mname::getEventType()
{
	return GEmname;
}



""".replace("mname", name))

writeLines("GameEvent.cpp", lines)

