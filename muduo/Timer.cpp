#include "Timer.h"

Timer::Timer(TimerCallback cb, Timestamp when, double interval) : 
    m_callback(std::move(cb)),
    m_expiration(when),
    m_interval(interval),
    m_repeat(interval > 0.0),
    m_sequence(++ m_sNumCreated){}

void Timer::restart(Timestamp now){
    if(m_repeat){
        m_expiration = add_time(now, m_interval);
    }
    else{
        m_expiration = Timestamp::invalid();
    }
}