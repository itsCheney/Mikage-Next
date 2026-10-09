#include "LayerUploadBatch.h"
#include <stdexcept>
#include <vector>
#include <limits>

void LayerUploadLayoutTests() {
    using namespace krkrsdl3::layer_upload;
    const auto require=[](bool b){if(!b) throw std::runtime_error("C3 upload allocation/ownership contract");};
    AtlasLayout atlas;int x=-1,y=-1;
    std::vector<unsigned char> coverage(size_t(AtlasSide)*AtlasSide);
    for(int i=0;i<100000;++i) {
        const int w=1+i%71,h=1+(i*7)%59;
        if(!atlas.Allocate(w,h,x,y)) break;
        require(x>=0 && y>=0 && x+w<=AtlasSide && y+h<=AtlasSide);
        for(int row=y;row<y+h;++row) for(int col=x;col<x+w;++col)
            require(coverage[size_t(row)*AtlasSide+col]++==0);
    }
    const auto saved=atlas;
    require(!atlas.Allocate(AtlasSide+1,1,x,y));
    require(atlas.x==saved.x && atlas.y==saved.y && atlas.rowHeight==saved.rowHeight);
    atlas={};require(atlas.Allocate(AtlasSide,AtlasSide,x,y) && x==0 && y==0);
    require(!atlas.Allocate(1,1,x,y));
    require(CanAppend(12,12,false) && !CanAppend(12,13,false) && !CanAppend(12,12,true));
    require(!CanRecycle(false,0) && !CanRecycle(true,1) && CanRecycle(true,0));
    ArenaLayout arena;std::vector<unsigned char> bytes(ArenaBytes,0);
    for(size_t i=1;i<4000;++i) {
        size_t offset=0,count=1+i%4097;
        if(!arena.Allocate(count,offset)) break;
        require(offset%256==0 && offset+count<=bytes.size());
        for(size_t j=offset;j<offset+count;++j) require(bytes[j]++==0);
    }
    const size_t before=arena.offset;size_t offset=99;
    require(!arena.Allocate(std::numeric_limits<size_t>::max(),offset) && arena.offset==before);
    arena={};require(arena.Allocate(ArenaBytes,offset) && !arena.Allocate(1,offset));
    size_t row=0,length=0;
    require(UploadSize(7,5,4,row,length) && row==256 && length==1280);
    require(UploadSize(1,256,1,row,length) && length==TinyBytes);
    require(UploadSize(1,257,1,row,length) && length>TinyBytes);
    require(!UploadSize(-1,2,4,row,length) && !UploadSize(1,1,3,row,length));
}
