from pathlib import Path
D=Path('artifacts/echo-save-continuity')
s=Path('src/ai/echo/Echo.cpp').read_text();s=s.replace('boost::logic::tribool passes=(*i)->passes_conditions(*this);','''boost::logic::tribool passes=(*i)->passes_conditions(*this);
        if(player->team->game->stepCounter>=6160 && player->team->game->stepCounter<=6175) {
            std::cerr << "TRACE building tick=" << player->team->game->stepCounter << " timer=" << timer << " id=" << (*i)->id << " type=" << (*i)->get_building_type() << " pass=" << (boost::logic::indeterminate(passes)?-1:(passes?1:0)) << " previous=" << previous_building_id << " gm_timer=" << gm->timer << " ages=";
            for(auto age:gm->ticks_since_update)std::cerr<<age<<",";
            std::cerr<<" queue=";auto pending=gm->queuedGradients;while(!pending.empty()){std::cerr<<pending.front()<<",";pending.pop();}std::cerr<<"\\n";
        }''')
(D/'Echo.cpp').write_text(s)
s=Path('src/ai/echo/Gradient.cpp').read_text();needle='''bool GradientManager::is_updated(const GradientInfo& gi)
{''';start=s.index(needle);end=s.index('\nvoid GradientManager::update()',start);part=s[start:end];part=part.replace('''if((*i)->get_gradient_info() == gi)
		{''','''if((*i)->get_gradient_info() == gi)
        {
            if(map->game->stepCounter>=6160 && map->game->stepCounter<=6175)
                std::cerr<<"TRACE query tick="<<map->game->stepCounter<<" index="<<(i-gradients.begin())<<" age="<<ticks_since_update[i-gradients.begin()]<<"\\n";''');s=s[:start]+part+s[end:];(D/'Gradient.cpp').write_text(s)
