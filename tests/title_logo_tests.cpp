#include "core/title_logo.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace conduit;
void require(bool value) { if (!value) throw std::runtime_error("Title logo assertion failed"); }
bool close(float a,float b) { return std::abs(a-b) < 0.001f; }
int main() {
    const auto contained = containTitleLogo({100,40,350,60},2004,686);
    require(contained.has_value());
    require(close(contained->x+contained->width/2,275));
    require(close(contained->y+contained->height/2,70));
    require(close(contained->width/contained->height,2004.0f/686));
    require(contained->width <= 350 && contained->height <= 60);
    const auto wide = containTitleLogo({0,0,20,100},2004,686);
    require(wide && close(wide->width,20) && close(wide->y+wide->height/2,50));
    require(!containTitleLogo({0,0,-1,10},2004,686));
    require(!containTitleLogo({0,0,10,0},2004,686));
    require(!containTitleLogo({0,0,10,10},0,686));
    require(!containTitleLogo({std::numeric_limits<float>::infinity(),0,10,10},2004,686));
    require(!containTitleLogo({0,0,10,std::numeric_limits<float>::quiet_NaN()},2004,686));
    require(isVanillaTitleImage(true,true,"title_image",0,"textures/ui/title",1937,333));
    require(!isVanillaTitleImage(false,true,"title_image",0,"textures/ui/title",1937,333));
    require(!isVanillaTitleImage(true,false,"title_image",0,"textures/ui/title",1937,333));
    require(!isVanillaTitleImage(true,true,"pause_title_image",0,"textures/ui/title",1937,333));
    require(!isVanillaTitleImage(true,true,"title_image",2,"textures/ui/title",1937,333));
    require(!isVanillaTitleImage(true,true,"title_image",0,"textures/custom/title",1937,333));
    require(!isVanillaTitleImage(true,true,"title_image",0,"textures/ui/title",2004,686));
    // Exact-build constructor fixture for the vanilla title path, RawPath filesystem.
    require(resourceLocationHash(2,0x375f08b33a3fe0adULL) == 0x983cb7ffbc3e5be8ULL);
    std::cout << "Title image eligibility, aspect-preserving placement, invalid bounds and location hash passed\n";
}
