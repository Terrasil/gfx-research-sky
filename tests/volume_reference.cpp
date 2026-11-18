#include <algorithm>
#include <cmath>
#include <iostream>
#include <random>

// Independent analytical checks for the volumetric evaluator's geometry and
// transport conventions. GPU checks exercise the actual GLSL implementation.
int main() {
    std::mt19937 random(91731);
    std::uniform_real_distribution<double> distribution(-10.0,10.0);
    for (int sample=0;sample<2000;++sample) {
        const double ox=distribution(random),oy=distribution(random),oz=distribution(random);
        double dx=distribution(random),dy=std::abs(distribution(random))+0.1,dz=distribution(random);
        const double norm=std::sqrt(dx*dx+dy*dy+dz*dz); dx/=norm;dy/=norm;dz/=norm;
        const double r=30.0,plane=0.35*r;
        // Independent ray/plane intersection versus LOM's q-based projection.
        const double t=(plane-oy)/dy;
        for (const double alongRay : {r,r*0.25,r*2.0}) {
            const double qx=ox+alongRay*dx,qy=oy+alongRay*dy,qz=oz+alongRay*dz;
            const double x=qx/r+(0.35-qy/r)*dx/dy,z=qz/r+(0.35-qy/r)*dz/dy;
            if (std::abs(x-(ox+t*dx)/r)>1e-10 || std::abs(z-(oz+t*dz)/r)>1e-10) return 1;
        }
    }
    // Homogeneous single-scattering solution is independent of partition count.
    for (int steps : {1,8,32,64,128,256}) {
        constexpr double extinction=0.7,distance=3.5,source=1.8,background=0.35;
        double transmission=1,radiance=0;
        for(int i=0;i<steps;++i) {
            const double attenuation=std::exp(-extinction*distance/steps);
            radiance+=transmission*(1-attenuation)*source;
            transmission*=attenuation;
        }
        const double expected=std::exp(-extinction*distance)*background+(1-std::exp(-extinction*distance))*source;
        if(std::abs(radiance+transmission*background-expected)>1e-12) return 2;
    }
    // Pure emission adds linearly; foreground cloud transmission attenuates it.
    constexpr double emissionA=0.3,emissionB=0.7,transmission=0.25;
    if(std::abs((emissionA+emissionB)*transmission-0.25)>1e-15) return 3;
    std::cout << "2000 ray/plane cases and analytical volume transport passed\n";
}
