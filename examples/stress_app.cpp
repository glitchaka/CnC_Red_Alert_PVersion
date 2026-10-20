#include <ra/Engine.h>
#include <ra/RTS.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {
struct ClickFx { float x{0}; float y{0}; float life{0}; };
enum class Screen { Splash, Menu, Game };
float clampf(float v,float lo,float hi){ return std::max(lo,std::min(v,hi)); }
bool inside(float x,float y,float l,float t,float r,float b){ return x>=l&&x<=r&&y>=t&&y<=b; }
ra::Rect tile16(int x,int y){ return {x*16.0f,y*16.0f,16.0f,16.0f}; }
ra::Rect cell10(int x,int y){ return {x*10.0f,y*10.0f,10.0f,10.0f}; }
}

class ClickerRTS final : public ra::Game {
public:
    void onStart(ra::Engine& e) override
    {
        // Exact user-provided sheets. No generated replacement art.
        plains_ = e.loadTexture("assets/mystic/plains.png");
        people_ = e.loadTexture("assets/bit-bonanza/People.png");
        ui_ = e.loadTexture("assets/bit-bonanza/UI.png");
        future_ = e.loadTexture("assets/bit-bonanza/Misc-Future.png");
        e.setWindowTitle("Click Command - Loading");
    }

    void onUpdate(ra::Engine& e,float dt) override
    {
        if(screen_==Screen::Splash){
            splash_+=dt;
            if(splash_>=0.8f||e.keyPressed(ra::Key::Enter)||e.mousePressed(ra::MouseButton::Left)){
                screen_=Screen::Menu;
                e.setWindowTitle("Click Command");
            }
            return;
        }
        if(screen_==Screen::Menu){ updateMenu(e); return; }
        updateGame(e,dt);
    }

    void onRender(ra::Engine& e) override
    {
        if(screen_==Screen::Splash) renderSplash(e);
        else if(screen_==Screen::Menu) renderMenu(e);
        else renderGame(e);
    }

private:
    static constexpr float Tile=32.0f;
    static constexpr int MapW=128;
    static constexpr int MapH=80;

    void updateMenu(ra::Engine& e)
    {
        if(e.keyPressed(ra::Key::Escape)){ e.quit(); return; }
        const float w=(float)e.windowWidth(),h=(float)e.windowHeight();
        const float bx=w*0.5f-210.0f,by=h*0.62f;
        if(e.keyPressed(ra::Key::Enter)||(e.mousePressed(ra::MouseButton::Left)&&inside(e.mouseX(),e.mouseY(),bx,by,bx+420,by+64))) startGame(e);
    }

    void startGame(ra::Engine& e)
    {
        screen_=Screen::Game;
        resources_=0;
        world_.clear();
        effects_.clear();
        zoom_=1.0f;
        drag_=false;
        cameraX_=baseX_-430.0f;
        cameraY_=baseY_-300.0f;
        spawn(baseX_+90,baseY_+120,false);
        spawn(baseX_-55,baseY_+130,false);
        spawn(baseX_+150,baseY_+70,false);
        e.setWindowTitle("Click Command - RTS Clicker");
    }

    int nextCost() const
    {
        const int bought=std::max(0,(int)world_.units().size()-3);
        return (int)std::ceil((double)baseCost_*std::pow(costGrowth_,bought));
    }

    void spawn(float x,float y,bool selected)
    {
        const auto id=world_.spawnUnit({x,y},0,98.0f);
        world_.setAutonomous(id,true,150.0f);
        if(selected){
            world_.clearSelection();
            if(auto* u=world_.find(id)) u->selected=true;
        }
    }

    void buyUnit()
    {
        const int cost=nextCost();
        if(resources_<cost) return;
        resources_-=cost;
        const int n=(int)world_.units().size();
        const float a=(float)n*0.86f;
        spawn(baseX_+std::cos(a)*150.0f,baseY_+std::sin(a)*120.0f,true);
    }

    void updateGame(ra::Engine& e,float dt)
    {
        if(e.keyPressed(ra::Key::Escape)){
            screen_=Screen::Menu;
            e.setWindowTitle("Click Command");
            return;
        }

        const float panelX=hudX(e),mx=e.mouseX(),my=e.mouseY();
        const float wheel=e.mouseWheelY();
        if(wheel!=0) zoom_=clampf(zoom_+wheel*0.12f,0.65f,2.6f);
        if(e.keyDown(ra::Key::Q)) zoom_=clampf(zoom_-dt,0.65f,2.6f);
        if(e.keyDown(ra::Key::E)) zoom_=clampf(zoom_+dt,0.65f,2.6f);

        float dx=0,dy=0;
        if(e.keyDown(ra::Key::A)||e.keyDown(ra::Key::Left)) dx-=1;
        if(e.keyDown(ra::Key::D)||e.keyDown(ra::Key::Right)) dx+=1;
        if(e.keyDown(ra::Key::W)||e.keyDown(ra::Key::Up)) dy-=1;
        if(e.keyDown(ra::Key::S)||e.keyDown(ra::Key::Down)) dy+=1;
        if(mx<10) dx-=1;
        if(mx<panelX&&mx>panelX-10) dx+=1;
        if(my<10) dy-=1;
        if(my>(float)e.windowHeight()-10) dy+=1;
        const float step=560.0f/zoom_*dt;
        cameraX_+=dx*step;
        cameraY_+=dy*step;
        clampCamera(e);

        const float bx=panelX+18,by=150,bw=(float)e.windowWidth()-panelX-36;
        if(e.mousePressed(ra::MouseButton::Left)){
            if(inside(mx,my,bx,by,bx+bw,by+72)) buyUnit();
            else if(mx<panelX){ drag_=true; sx_=ex_=mx; sy_=ey_=my; }
        }
        if(drag_&&e.mouseDown(ra::MouseButton::Left)){ ex_=mx; ey_=my; }
        if(drag_&&e.mouseReleased(ra::MouseButton::Left)){
            ex_=mx; ey_=my;
            const float ddx=ex_-sx_,ddy=ey_-sy_;
            if(ddx*ddx+ddy*ddy<=36){
                ++resources_;
                effects_.push_back({mx,my,0.7f});
                world_.selectPoint({screenToWorldX(mx),screenToWorldY(my)},24.0f/zoom_,false);
            }else{
                world_.selectRect({screenToWorldX(std::min(sx_,ex_)),screenToWorldY(std::min(sy_,ey_)),screenToWorldX(std::max(sx_,ex_)),screenToWorldY(std::max(sy_,ey_))},false);
            }
            drag_=false;
        }
        if(e.mousePressed(ra::MouseButton::Right)&&mx<panelX)
            world_.commandMove({screenToWorldX(mx),screenToWorldY(my)},30.0f);

        world_.update(dt);
        for(auto& fx:effects_){ fx.life-=dt; fx.y-=26.0f*dt; }
        effects_.erase(std::remove_if(effects_.begin(),effects_.end(),[](const ClickFx& fx){ return fx.life<=0; }),effects_.end());
    }

    void renderSplash(ra::Engine& e)
    {
        const float w=(float)e.windowWidth(),h=(float)e.windowHeight();
        e.clear({8,11,9,255});
        for(int y=0;y<6;++y) for(int x=0;x<16;++x)
            e.drawTextureRegion(plains_,tile16(4,4),{w*0.5f-256+x*32.0f,h*0.18f+y*32.0f,32,32});
        e.drawText(w*0.5f-58,h*0.46f,"CLICK COMMAND",{246,238,203,255});
        e.drawText(w*0.5f-82,h*0.50f,"MYSTIC WOODS + BIT BONANZA",{200,220,195,255});
        e.fillRect({w*0.5f-180,h*0.58f,360,12},{30,39,33,255});
        e.fillRect({w*0.5f-180,h*0.58f,360*clampf(splash_/0.8f,0,1),12},{83,160,59,255});
    }

    void renderMenu(ra::Engine& e)
    {
        const float w=(float)e.windowWidth(),h=(float)e.windowHeight();
        e.clear({9,13,11,255});
        for(int y=0;y<10;++y) for(int x=0;x<22;++x){
            const int tx=(x+y)%11==0?5:4;
            const int ty=tx==5?5:4;
            e.drawTextureRegion(plains_,tile16(tx,ty),{w*0.5f-352+x*32.0f,h*0.10f+y*32.0f,32,32});
        }
        e.fillRect({w*0.5f-290,h*0.42f,580,300},{12,18,14,225});
        e.drawText(w*0.5f-58,h*0.47f,"CLICK COMMAND",{245,238,202,255});
        e.drawText(w*0.5f-96,h*0.51f,"RTS + CLICKER ECONOMY",{188,211,187,255});
        const float bx=w*0.5f-210,by=h*0.62f;
        const bool hot=inside(e.mouseX(),e.mouseY(),bx,by,bx+420,by+64);
        e.fillRect({bx,by,420,64},hot?ra::Color{62,120,68,255}:ra::Color{43,88,50,255});
        e.drawRect({bx,by,420,64},{126,164,130,255});
        e.drawText(bx+170,by+28,"START GAME",{247,244,220,255});
        e.drawText(w*0.5f-112,by+92,"ENTER Start    ESC Exit",{150,170,154,255});
    }

    void renderGame(ra::Engine& e)
    {
        const float vw=hudX(e),vh=(float)e.windowHeight();
        e.clear({5,7,6,255});
        drawTerrain(e,vw,vh);
        drawBase(e);
        drawUnits(e,vw,vh);
        if(drag_){
            const float l=std::min(sx_,ex_),r=std::max(sx_,ex_),t=std::min(sy_,ey_),b=std::max(sy_,ey_);
            e.drawRect({l,t,r-l,b-t},{96,238,104,255});
        }
        for(const auto& fx:effects_) e.drawText(fx.x-5,fx.y,"+1",{255,218,65,255});
        drawHud(e,vw,vh);
    }

    void drawTerrain(ra::Engine& e,float viewW,float viewH)
    {
        const int firstX=std::max(0,(int)std::floor(cameraX_/Tile)-1);
        const int firstY=std::max(0,(int)std::floor(cameraY_/Tile)-1);
        const int lastX=std::min(MapW-1,firstX+(int)(viewW/(Tile*zoom_))+4);
        const int lastY=std::min(MapH-1,firstY+(int)(viewH/(Tile*zoom_))+4);

        for(int y=firstY;y<=lastY;++y){
            for(int x=firstX;x<=lastX;++x){
                int sx=4,sy=4; // plain grass from original plains.png
                const bool roadH=(y>=30&&y<=33&&x>2&&x<MapW-3);
                const bool roadV=(x>=54&&x<=57&&y>4&&y<MapH-4);
                if(roadH||roadV){ sx=4; sy=0; } // original dirt tile
                drawTile(e,x,y,sx,sy);

                // Original decorative tiles. Premium rows 8-11 are never sampled.
                if(!roadH&&!roadV){
                    const int hash=(x*37+y*71)%257;
                    if(hash==11) drawTile(e,x,y,5,5);
                    else if(hash==29) drawTile(e,x,y,5,4);
                    else if(hash==47) drawTile(e,x,y,0,3);
                }
            }
        }
    }

    void drawTile(ra::Engine& e,int mapX,int mapY,int sheetX,int sheetY)
    {
        const float s=Tile*zoom_;
        e.drawTextureRegion(plains_,tile16(sheetX,sheetY),{(mapX*Tile-cameraX_)*zoom_,(mapY*Tile-cameraY_)*zoom_,s,s});
    }

    void drawBase(ra::Engine& e)
    {
        // Base footprint assembled with non-premium original Mystic Woods tiles.
        for(int y=0;y<5;++y){
            for(int x=0;x<7;++x){
                int sx=4,sy=0;
                if(y==0){ sx=(x==0?1:(x==6?3:2)); sy=0; }
                else if(y==4){ sx=(x==0?1:(x==6?3:2)); sy=3; }
                else if(x==0){ sx=0; sy=1; }
                else if(x==6){ sx=3; sy=1; }
                drawTile(e,23+x,18+y,sx,sy);
            }
        }

        // Visible machinery uses the exact Bit Bonanza Misc-Future sheet.
        const float px=(24*Tile-cameraX_)*zoom_,py=(19*Tile-cameraY_)*zoom_;
        const float cell=36.0f*zoom_;
        for(int y=0;y<3;++y) for(int x=0;x<4;++x)
            e.drawTextureRegion(future_,cell10((x+1)%4,(y+2)%9),{px+x*cell,py+y*cell,cell,cell});
        e.drawText(px+10,py-18,"BASE",{250,239,199,255});
    }

    void drawUnits(ra::Engine& e,float viewW,float viewH)
    {
        const float size=30.0f*zoom_;
        int i=0;
        for(const auto& u:world_.units()){
            const float x=(u.position.x-cameraX_)*zoom_,y=(u.position.y-cameraY_)*zoom_;
            if(x<-size||y<-size||x>viewW+size||y>viewH+size){ ++i; continue; }
            if(u.selected){
                const float tx=(u.destination.x-cameraX_)*zoom_,ty=(u.destination.y-cameraY_)*zoom_;
                e.drawLine(x,y,tx,ty,{83,208,95,150});
                e.drawRect({x-size*0.62f,y-size*0.62f,size*1.24f,size*1.24f},{92,242,106,255});
            }
            // Exact 10x10 people sprites, varied across the provided sheet.
            const int sx=(i%6)*2;
            const int sy=(i%9)*3;
            e.drawTextureRegion(people_,cell10(sx,sy),{x-size*0.5f,y-size*0.5f,size,size});
            ++i;
        }
    }

    void drawHud(ra::Engine& e,float x,float h)
    {
        const float pw=(float)e.windowWidth()-x;
        e.fillRect({x,0,pw,h},{14,19,16,255});
        e.drawText(x+18,18,"CLICK COMMAND",{245,238,203,255});
        e.drawText(x+18,52,"RESOURCES",{160,180,164,255});
        e.drawText(x+18,77,std::to_string(resources_),{255,216,64,255});
        e.drawText(x+18,104,"CLICK MAP: +1",{132,153,138,255});

        const int cost=nextCost();
        const bool can=resources_>=cost;
        const float bx=x+18,by=150,bw=pw-36;
        e.fillRect({bx,by,bw,72},can?ra::Color{43,92,51,255}:ra::Color{44,49,45,255});
        e.drawRect({bx,by,bw,72},{105,133,111,255});
        e.drawTextureRegion(people_,cell10(0,0),{bx+12,by+12,44,44});
        e.drawText(bx+70,by+18,"BASIC UNIT",{243,242,218,255});
        e.drawText(bx+70,by+44,("COST: "+std::to_string(cost)).c_str(),can?ra::Color{255,216,64,255}:ra::Color{132,138,132,255});

        e.drawText(x+18,248,("UNITS: "+std::to_string(world_.units().size())).c_str(),{190,203,193,255});
        e.drawText(x+18,272,("SELECTED: "+std::to_string(world_.selectedCount())).c_str(),{190,203,193,255});
        e.drawText(x+18,296,"UNIT AI: ACTIVE",{96,220,110,255});
        e.drawText(x+18,320,"COST GROWTH: 18%",{151,168,156,255});

        e.drawText(x+18,368,"LMB       +1 resource",{160,177,165,255});
        e.drawText(x+18,392,"DRAG LMB  select",{160,177,165,255});
        e.drawText(x+18,416,"RMB       move",{160,177,165,255});
        e.drawText(x+18,440,"WASD      camera",{160,177,165,255});
        e.drawText(x+18,464,"WHEEL     zoom",{160,177,165,255});

        // Real UI sheet strip is visible instead of invented cyan bars.
        e.drawTextureRegion(ui_,{0,130,200,10},{x+18,505,pw-36,18});

        const float mx=x+18,my=h-180,mw=pw-36,mh=120;
        e.drawText(mx,my-20,"MINIMAP",{220,226,212,255});
        e.fillRect({mx,my,mw,mh},{45,86,47,255});
        for(const auto& u:world_.units())
            e.fillRect({mx+u.position.x/worldW_*mw-1,my+u.position.y/worldH_*mh-1,3,3},u.selected?ra::Color{90,245,103,255}:ra::Color{240,229,181,255});
        e.drawRect({mx+cameraX_/worldW_*mw,my+cameraY_/worldH_*mh,std::max(8.0f,(hudX(e)/zoom_)/worldW_*mw),std::max(8.0f,((float)e.windowHeight()/zoom_)/worldH_*mh)},{242,242,218,255});
    }

    float hudX(const ra::Engine& e) const { return (float)e.windowWidth()-320.0f; }
    float screenToWorldX(float x) const { return cameraX_+x/zoom_; }
    float screenToWorldY(float y) const { return cameraY_+y/zoom_; }
    void clampCamera(const ra::Engine& e)
    {
        const float vw=hudX(e)/zoom_,vh=(float)e.windowHeight()/zoom_;
        cameraX_=clampf(cameraX_,0,std::max(0.0f,worldW_-vw));
        cameraY_=clampf(cameraY_,0,std::max(0.0f,worldH_-vh));
    }

    Screen screen_{Screen::Splash};
    float splash_{0};
    int resources_{0};
    ra::rts::World world_{{0,0,MapW*Tile,MapH*Tile}};
    std::vector<ClickFx> effects_;
    float cameraX_{0},cameraY_{0},zoom_{1};
    bool drag_{false};
    float sx_{0},sy_{0},ex_{0},ey_{0};
    const float worldW_=MapW*Tile,worldH_=MapH*Tile;
    const float baseX_=26*Tile,baseY_=20*Tile;
    const int baseCost_=10;
    const double costGrowth_=1.18;
    ra::Texture plains_,people_,ui_,future_;
};

int main()
{
    ra::EngineConfig c;
    c.title="Click Command";
    c.width=1440;
    c.height=900;
    c.resizable=true;
    c.vsync=true;
    try{ ra::Engine e(c); ClickerRTS game; return e.run(game); }
    catch(const std::exception& ex){ std::fprintf(stderr,"Startup failed: %s\n",ex.what()); return 1; }
}
