#pragma once

#include <array>
#include <glad/gl.h>

// Only report newly resolved samples. Never read an unsubmitted query or count
// the previous result again when the GPU is still processing an expensive sky.
class ExperimentGpuTimer {
public:
    ~ExperimentGpuTimer() { if (queries_[0]) glDeleteQueries(16, queries_.data()); }
    void reset() { pending_.fill(false); fresh_=false; }
    void begin() {
        fresh_=false;
        if (!queries_[0]) glCreateQueries(GL_TIMESTAMP, 16, queries_.data());
        resolve();
        active_=!pending_[slot_];
        if (active_) glQueryCounter(queries_[slot_*2], GL_TIMESTAMP);
    }
    void end() {
        if (active_) {
            glQueryCounter(queries_[slot_*2+1], GL_TIMESTAMP);
            pending_[slot_]=true;
            slot_=(slot_+1)%8;
        }
        resolve();
    }
    bool has_result() const { return fresh_; }
    double milliseconds() const { return milliseconds_; }
private:
    void resolve() {
        if (fresh_) return;
        // Prefer older entries; each result is consumed exactly once.
        for (int offset=0; offset<8; ++offset) {
            const int index=(slot_+offset)%8;
            if (!pending_[index]) continue;
            GLint available=GL_FALSE;
            glGetQueryObjectiv(queries_[index*2+1], GL_QUERY_RESULT_AVAILABLE, &available);
            if (!available) continue;
            GLuint64 first=0,last=0;
            glGetQueryObjectui64v(queries_[index*2],GL_QUERY_RESULT,&first);
            glGetQueryObjectui64v(queries_[index*2+1],GL_QUERY_RESULT,&last);
            pending_[index]=false;
            if(last>=first) {milliseconds_=double(last-first)/1000000.0;fresh_=true;}
            return;
        }
    }
    std::array<GLuint,16> queries_{};
    std::array<bool,8> pending_{};
    int slot_=0;
    bool active_=false,fresh_=false;
    double milliseconds_=0.0;
};
