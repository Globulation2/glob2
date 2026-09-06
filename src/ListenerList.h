// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <list>

///Non-owning observer list. notify() walks the live list in registration order.
template<typename T>
class ListenerList
{
public:
	void add(T* listener)
	{
		listeners.push_back(listener);
	}

	///No-op if the listener is not registered
	void remove(T* listener)
	{
		listeners.remove(listener);
	}

	///Calls (listener->*method)(args...) on every listener
	template<typename Method, typename... Args>
	void notify(Method method, Args&&... args)
	{
		for(T* listener : listeners)
		{
			(listener->*method)(args...);
		}
	}

private:
	std::list<T*> listeners;
};
