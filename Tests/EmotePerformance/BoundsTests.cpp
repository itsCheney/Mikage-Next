#include "emotegeometrybounds.h"
#include <cstdlib>
#include <iostream>
#include <random>

using namespace emoteplayer::performance;
namespace experiment = emoteplayer::performance::bounds_experiment;
static int checks=0;
static void check(bool condition,const char* message) {
    ++checks;
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
static bool equal(Rect a,Rect b) {
    return a.left==b.left && a.top==b.top && a.right==b.right && a.bottom==b.bottom;
}
struct Vertex { float x=0,y=0; };
struct GPUSurface {
    int type=0;
    float matrix[16]{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    float controlPts[32]{};
    float originX=0,originY=0,width=1,height=1;
};
struct Node {
    bool wasDrawn=false,_useGPUDeform=false;
    std::vector<Vertex> _meshVertices;
    std::vector<std::uint16_t> _meshIndices;
    std::vector<GPUSurface> _gpuDeformSurfaces;
};
static long double bernstein(int i,long double t) {
    const long double a=1-t;
    switch(i) { case 0:return a*a*a; case 1:return 3*t*a*a; case 2:return 3*t*t*a; default:return t*t*t; }
}
static std::array<long double,2> evaluate(const std::vector<experiment::Surface>& surfaces,long double u,long double v) {
    std::array<long double,4> trans{{1,1,1,1}};
    for (int n=int(surfaces.size())-1;n>=0;--n) {
        const auto& s=surfaces[n];
        if (s.type!=3) {
            const long double inputU=n==int(surfaces.size())-1 ? u : (trans[1]+s.originY)/s.height;
            const long double inputV=n==int(surfaces.size())-1 ? v : (trans[0]+s.originX)/s.width;
            long double x=inputV,y=inputU;
            if (s.type==1) {
                x=y=0;
                for(int row=0;row<4;++row) for(int col=0;col<4;++col) {
                    const auto weight=bernstein(row,inputU)*bernstein(col,inputV);
                    x+=s.control[(row*4+col)*2]*weight;
                    y+=s.control[(row*4+col)*2+1]*weight;
                }
            }
            trans={{x,y,0,1}};
        }
        std::array<long double,4> out{};
        for(int row=0;row<4;++row) for(int col=0;col<4;++col)
            out[row]+=trans[col]*s.matrix[col*4+row];
        trans=out;
    }
    return {{trans[0],-trans[1]}};
}
static void contained(const experiment::Result& bounds,const std::array<long double,2>& p) {
    if (!(bounds.known && p[0]>=bounds.x.lo && p[0]<=bounds.x.hi && p[1]>=bounds.y.lo && p[1]<=bounds.y.hi))
        std::cerr << "sample " << checks << ": x=" << double(p[0]) << " in [" << bounds.x.lo << ',' << bounds.x.hi
                  << "], y=" << double(p[1]) << " in [" << bounds.y.lo << ',' << bounds.y.hi << "]\n";
    check(bounds.known && p[0]>=bounds.x.lo && p[0]<=bounds.x.hi && p[1]>=bounds.y.lo && p[1]<=bounds.y.hi,
          "restricted Bernstein intervals must contain every mathematical surface sample");
}

int main() {
    Node sprite;
    sprite.wasDrawn=true;
    sprite._meshVertices={{-.5f,-.5f},{.25f,-.5f},{.25f,.5f},{-.5f,.5f}};
    sprite._meshIndices={0,1,2,1,2,3};
    std::vector<Node*> nodes{&sprite};
    check(equal(submittedCPUBounds(nodes,200,100).rect,{49,24,126,76}),"submitted affine vertices include outward rounding and one pixel margin");
    Node second=sprite;
    second._meshVertices={{-.9f,.6f},{-.8f,.9f},{-.8f,.6f}};
    second._meshIndices={0,1,2};
    nodes.push_back(&second);
    const auto pair=submittedCPUBounds(nodes,200,100);
    check(pair.known && pair.rect.left<=10 && pair.rect.bottom>=95,"all submitted players/nodes enlarge damage bounds");
    second.wasDrawn=false;
    check(equal(submittedCPUBounds(nodes,200,100).rect,{49,24,126,76}),"undrawn interaction-only shapes are excluded");
    second._useGPUDeform=true;
    check(submittedCPUBounds(nodes,200,100).known,"hidden GPU geometry does not invalidate visible CPU bounds");
    second.wasDrawn=true;
    check(!submittedCPUBounds(nodes,200,100).known,"visible GPU-deformed nodes always use full production capture");
    second.wasDrawn=false;
    nodes={&sprite};
    sprite._meshIndices[0]=99;
    check(!submittedCPUBounds(nodes,200,100).known,"invalid submitted indices disable cropping");
    sprite._meshIndices[0]=0;
    sprite._meshVertices[0].x=std::numeric_limits<float>::quiet_NaN();
    check(!submittedCPUBounds(nodes,200,100).known,"NaN vertex cannot produce unsafe integer damage");
    sprite._meshVertices[0].x=-std::numeric_limits<float>::max();
    sprite._meshVertices[1].x=std::numeric_limits<float>::max();
    const auto extreme=submittedCPUBounds(nodes,200,100);
    check(extreme.known && extreme.rect.left==0 && extreme.rect.right==200,"large finite coordinates clip before integer conversion");
    check(!submittedCPUBounds(nodes,0,100).known,"invalid target dimensions reject bounds");
    sprite.wasDrawn=false;
    check(submittedCPUBounds(nodes,200,100).known && submittedCPUBounds(nodes,200,100).rect.empty(),"no submitted pixels have known empty bounds");

    experiment::Surface affine;
    affine.type=2;
    affine.matrix[0]=1.5; affine.matrix[5]=2; affine.matrix[12]=-.75; affine.matrix[13]=-1;
    auto affineBounds=experiment::surfaceChain({affine});
    for(int y=0;y<=20;++y) for(int x=0;x<=20;++x)
        contained(affineBounds,evaluate({affine},x/20.L,y/20.L));
    check(affineBounds.x.lo<=-.75 && affineBounds.x.hi>=.75,"affine chain produces conservative extent");
    auto zero=affine; zero.width=0;
    check(!experiment::surfaceChain({zero}).known,"zero dimensions reject experimental normalization");
    auto invalid=affine; invalid.matrix[7]=std::numeric_limits<double>::infinity();
    check(!experiment::surfaceChain({invalid}).known,"nonfinite matrix rejects experimental bound");
    auto unsupported=affine; unsupported.type=4;
    check(!experiment::surfaceChain({unsupported}).known,"unsupported surface type rejects experimental bound");
    check(!experiment::surfaceChain({}).known,"missing GPU chain remains unknown");
    check(!experiment::surfaceChain(std::vector<experiment::Surface>(33,affine)).known,"excessive chain depth has bounded CPU cost and full fallback");

    std::mt19937 rng(0x4d4f5445);
    std::uniform_real_distribution<double> value(-2,2);
    for(int trial=0;trial<80;++trial) {
        std::vector<experiment::Surface> chain(3);
        for(std::size_t i=0;i<chain.size();++i) {
            auto& surface=chain[i];
            surface.type=i==1 && trial%3==0 ? 3 : 1;
            surface.originX=value(rng); surface.originY=value(rng);
            surface.width=trial%2 ? .6 : -.8;
            surface.height=trial%2 ? -.7 : .9;
            for(double& control:surface.control) control=value(rng);
            surface.matrix[0]=1+value(rng)*.2;
            surface.matrix[5]=1+value(rng)*.2;
            surface.matrix[4]=value(rng)*.2;
            surface.matrix[1]=value(rng)*.2;
            surface.matrix[12]=value(rng); surface.matrix[13]=value(rng);
        }
        const auto bounds=experiment::surfaceChain(chain);
        check(bounds.known,"finite deformation/extrapolation chain has a candidate bound");
        // Parents frequently receive UVs outside [0,1]. The implementation
        // reparameterizes that interval instead of clamping away visible pixels.
        for(int y=0;y<=16;++y) for(int x=0;x<=16;++x)
            contained(bounds,evaluate(chain,x/16.L,y/16.L));
    }
    Node gpu;
    gpu.wasDrawn=true; gpu._useGPUDeform=true;
    GPUSurface gpuAffine;
    gpuAffine.type=2; gpuAffine.matrix[0]=1; gpuAffine.matrix[5]=1;
    gpuAffine.matrix[12]=-.5f; gpuAffine.matrix[13]=-.5f;
    gpu._gpuDeformSurfaces={gpuAffine};
    nodes={&gpu};
    const auto candidate=submittedExperimentalBounds(nodes,200,100);
    check(candidate.known && candidate.rect.left<=49 && candidate.rect.right>=151,"candidate GPU bound can be measured without creating a CPU mesh");
    check(!submittedCPUBounds(nodes,200,100).known,"successful candidate never changes production GPU fallback");
    gpu._gpuDeformSurfaces[0].width=0;
    check(!submittedExperimentalBounds(nodes,200,100).known,"invalid GPU candidate remains unknown");
    std::cout << "Bounds: " << checks << " checks passed\n";
}
