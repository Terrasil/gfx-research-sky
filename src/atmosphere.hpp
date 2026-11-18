#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>
#include <glm/vec4.hpp>

struct AtmosphereConstants { glm::vec4 zenith; glm::vec4 normalization; };

// Preetham et al., Appendix A.2. Same frame-constant calculation as LOM Sky.hpp.
inline AtmosphereConstants atmosphereConstants(float sunHeight, float t, float exposure) {
    const float theta=std::acos(std::clamp(sunHeight,0.001f,1.0f));
    const float theta2=theta*theta, theta3=theta2*theta;
    const float x=(0.00166f*theta3-0.00375f*theta2+0.00209f*theta)*t*t
        +(-0.02903f*theta3+0.06377f*theta2-0.03202f*theta+0.00394f)*t
        +(0.11693f*theta3-0.21196f*theta2+0.06052f*theta+0.25886f);
    const float y=(0.00275f*theta3-0.00610f*theta2+0.00317f*theta)*t*t
        +(-0.04214f*theta3+0.08970f*theta2-0.04153f*theta+0.00516f)*t
        +(0.15346f*theta3-0.26756f*theta2+0.06670f*theta+0.26688f);
    const float luminance=(4.0453f*t-4.9710f)*std::tan((4.0f/9.0f-t/120.0f)
        *(std::numbers::pi_v<float>-2.0f*theta))-0.2155f*t+2.4192f;
    const auto perez=[theta](float a,float b,float c,float d,float e) {
        const float cosSun=std::cos(theta);
        return (1+a*std::exp(b))*(1+c*std::exp(d*theta)+e*cosSun*cosSun);
    };
    return {{t,std::max(luminance,0.0f),x,y},{
        1/perez(0.1787f*t-1.4630f,-0.3554f*t+0.4275f,-0.0227f*t+5.3251f,0.1206f*t-2.5771f,-0.0670f*t+0.3703f),
        1/perez(-0.0193f*t-0.2592f,-0.0665f*t+0.0008f,-0.0004f*t+0.2125f,-0.0641f*t-0.8989f,-0.0033f*t+0.0452f),
        1/perez(-0.0167f*t-0.2608f,-0.0950f*t+0.0092f,-0.0079f*t+0.2102f,-0.0441f*t-1.6537f,-0.0109f*t+0.0529f),exposure}};
}
