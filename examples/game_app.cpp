#include <ra/Engine.h>
#include <ra/Net.h>
#include <ra/RTS.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace {
struct ClickFx { float x{}, y{}, life{}; };
enum class Screen { Splash, Menu, Lobby, Game };
enum class NetworkMode { Offline, Host, Client };

float clampf(float v, float lo, float hi) { return std::max(lo, std::min(v, hi)); }
bool inside(float x,float y,float l,float t,float r,float b){ return x>=l&&x<=r&&y>=t&&y<=b; }
ra::Rect tile16(int x,int y){ return {x*16.0f,y*16.0f,16.0f,16.0f}; }
ra::Rect frame48(int x,int y){ return {x*48.0f,y*48.0f,48.0f,48.0f}; }
ra::Rect frame32(int x,int y){ return {x*32.0f,y*32.0f,32.0f,32.0f}; }
constexpr std::uint16_t LanPort=46000;
constexpr const char* Discover="CC_DISCOVER_3";
constexpr const char* Welcome="CC_WELCOME_3";

bool hasId(const std::vector<ra::rts::UnitId>& ids,ra::rts::UnitId id){return std::find(ids.begin(),ids.end(),id)!=ids.end();}
}

class ClickerRTS final : public ra::Game {
public:
    void onStart(ra::Engine& e) override {
        e.setWindowTitle("Click Command - Mystic Woods");
        grass_=load(e,"assets/mystic/tilesets/grass.png");
        plains_=load(e,"assets/mystic/plains.png");
        decor_=load(e,"assets/mystic/tilesets/decor_16x16.png");
        fences_=load(e,"assets/mystic/tilesets/fences.png");
        walls_=load(e,"assets/mystic/tilesets/walls.png");
        flooring_=load(e,"assets/mystic/tilesets/flooring.png");
        wooden_=load(e,"assets/mystic/tilesets/wooden.png");
        chest_=load(e,"assets/mystic/objects/chest_01.png");
        player_=load(e,"assets/mystic/characters/player.png");
        slime_=load(e,"assets/mystic/characters/slime.png");
        if(!assetError_.empty()){
            std::ofstream out("RA-Framework-error.txt",std::ios::trunc); out<<assetError_;
        }
    }
    void onStop(ra::Engine&) override { socket_.close(); }

    void onUpdate(ra::Engine& e,float dt) override {
        if(screen_==Screen::Splash){
            splash_+=dt;
            if(splash_>.45f||e.keyPressed(ra::Key::Enter)||e.mousePressed(ra::MouseButton::Left))screen_=Screen::Menu;
            return;
        }
        if(screen_==Screen::Menu){ updateMenu(e); return; }
        if(screen_==Screen::Lobby){ updateLobby(e,dt); return; }
        updateGame(e,dt);
    }

    void onRender(ra::Engine& e) override {
        if(screen_==Screen::Splash) renderSplash(e);
        else if(screen_==Screen::Menu) renderMenu(e);
        else if(screen_==Screen::Lobby) renderLobby(e);
        else renderGame(e);
    }

private:
    static constexpr float Tile=32.0f;
    static constexpr int MapW=128,MapH=80;

    ra::Texture load(ra::Engine& e,const char* p){
        try{return e.loadTexture(p);}catch(const std::exception& ex){assetError_+=std::string(p)+": "+ex.what()+"\n";return{};}
    }

    void updateMenu(ra::Engine& e){
        if(e.keyPressed(ra::Key::Escape)){e.quit();return;}
        const float w=(float)e.windowWidth(),h=(float)e.windowHeight(),x=w*.5f-210,y=h*.52f;
        if(e.keyPressed(ra::Key::Enter)||(e.mousePressed(ra::MouseButton::Left)&&inside(e.mouseX(),e.mouseY(),x,y,x+420,y+62))){mode_=NetworkMode::Offline;startGame(e);return;}
        if(e.mousePressed(ra::MouseButton::Left)&&inside(e.mouseX(),e.mouseY(),x,y+76,x+420,y+138)){startHost(e);return;}
        if(e.mousePressed(ra::MouseButton::Left)&&inside(e.mouseX(),e.mouseY(),x,y+152,x+420,y+214)){startJoin(e);}
    }

    void startHost(ra::Engine& e){
        socket_.close();mode_=NetworkMode::Host;connected_=false;peer_={};
        lobbyStatus_=socket_.open(LanPort,true)?"WAITING FOR LAN PLAYER...":"HOST ERROR: "+socket_.lastError();
        screen_=Screen::Lobby;e.setWindowTitle("Click Command - Host LAN");
    }
    void startJoin(ra::Engine& e){
        socket_.close();mode_=NetworkMode::Client;connected_=false;peer_={};discoverTimer_=0;
        lobbyStatus_=socket_.open(0,true)?"SEARCHING LAN HOST...":"JOIN ERROR: "+socket_.lastError();
        screen_=Screen::Lobby;e.setWindowTitle("Click Command - Join LAN");
    }
    void updateLobby(ra::Engine& e,float dt){
        if(e.keyPressed(ra::Key::Escape)){socket_.close();mode_=NetworkMode::Offline;screen_=Screen::Menu;return;}
        if(!socket_.isOpen())return;
        if(mode_==NetworkMode::Client){
            discoverTimer_-=dt;
            if(discoverTimer_<=0){discoverTimer_=.5f;ra::net::Endpoint b{"255.255.255.255",LanPort};socket_.send(b,Discover,std::char_traits<char>::length(Discover));}
        }
        char buf[1024];ra::net::Endpoint from;int n=0;
        while((n=socket_.receive(buf,sizeof(buf),from))>0){
            std::string m(buf,buf+n);
            if(mode_==NetworkMode::Host&&m==Discover){peer_=from;socket_.send(peer_,Welcome,std::char_traits<char>::length(Welcome));connected_=true;startGame(e);return;}
            if(mode_==NetworkMode::Client&&m==Welcome){peer_=from;connected_=true;startGame(e);return;}
        }
    }

    void startGame(ra::Engine& e){
        screen_=Screen::Game;resources_=0;world_.clear();effects_.clear();zoom_=1;drag_=false;snapshotTimer_=0;
        cameraX_=baseX_-420;cameraY_=baseY_-285;
        if(mode_!=NetworkMode::Client){
            spawn(baseX_+120,baseY_+120,0);spawn(baseX_-70,baseY_+135,0);spawn(baseX_+170,baseY_+55,0);
            if(mode_==NetworkMode::Host){spawn(enemyX_+120,enemyY_+120,1);spawn(enemyX_-70,enemyY_+135,1);spawn(enemyX_+170,enemyY_+55,1);}
        }
        e.setWindowTitle(mode_==NetworkMode::Offline?"Click Command - Mystic Woods RTS":"Click Command - Mystic Woods LAN");
    }

    int localTeam()const{return mode_==NetworkMode::Client?1:0;}
    int teamCount(int team)const{int n=0;for(const auto&u:world_.units())if(u.team==team)++n;return n;}
    int nextCost(int team)const{return(int)std::ceil(10.0*std::pow(1.18,std::max(0,teamCount(team)-3)));}
    void spawn(float x,float y,int team){auto id=world_.spawnUnit({x,y},team,team==0?105.0f:90.0f);world_.setAutonomous(id,true,145.0f);}
    void buy(int team){int c=nextCost(team);if(resources_<c)return;resources_-=c;int n=teamCount(team);float a=n*.91f;float bx=team?enemyX_:baseX_,by=team?enemyY_:baseY_;spawn(bx+std::cos(a)*155,by+std::sin(a)*120,team);}

    void updateGame(ra::Engine& e,float dt){
        if(e.keyPressed(ra::Key::Escape)){socket_.close();connected_=false;mode_=NetworkMode::Offline;screen_=Screen::Menu;return;}
        pumpNetwork();
        float panel=hudX(e),mx=e.mouseX(),my=e.mouseY();
        if(e.mouseWheelY()!=0)zoom_=clampf(zoom_+e.mouseWheelY()*.12f,.65f,2.5f);
        float dx=0,dy=0;
        if(e.keyDown(ra::Key::A)||e.keyDown(ra::Key::Left))dx--;
        if(e.keyDown(ra::Key::D)||e.keyDown(ra::Key::Right))dx++;
        if(e.keyDown(ra::Key::W)||e.keyDown(ra::Key::Up))dy--;
        if(e.keyDown(ra::Key::S)||e.keyDown(ra::Key::Down))dy++;
        if(mx<8)dx--;if(mx<panel&&mx>panel-8)dx++;if(my<8)dy--;if(my>e.windowHeight()-8)dy++;
        float step=560.0f/zoom_*dt;cameraX_+=dx*step;cameraY_+=dy*step;clampCamera(e);

        float bx=panel+18,by=154,bw=e.windowWidth()-panel-36;
        if(e.mousePressed(ra::MouseButton::Left)){
            if(inside(mx,my,bx,by,bx+bw,by+76)){if(mode_==NetworkMode::Client)send("B");else buy(localTeam());}
            else if(mx<panel){drag_=true;sx_=ex_=mx;sy_=ey_=my;}
        }
        if(drag_&&e.mouseDown(ra::MouseButton::Left)){ex_=mx;ey_=my;}
        if(drag_&&e.mouseReleased(ra::MouseButton::Left)){
            ex_=mx;ey_=my;float ax=ex_-sx_,ay=ey_-sy_;
            if(ax*ax+ay*ay<36){if(mode_==NetworkMode::Client)send("C");else ++resources_;effects_.push_back({mx,my,.7f});world_.selectPoint({wX(mx),wY(my)},26.0f/zoom_,false);}
            else world_.selectRect({wX(std::min(sx_,ex_)),wY(std::min(sy_,ey_)),wX(std::max(sx_,ex_)),wY(std::max(sy_,ey_))},false);
            for(auto&u:world_.units())if(u.selected&&u.team!=localTeam())u.selected=false;drag_=false;
        }
        if(e.mousePressed(ra::MouseButton::Right)&&mx<panel){float x=wX(mx),y=wY(my);if(mode_==NetworkMode::Client)sendMove(x,y);else world_.commandMove({x,y},34);}
        if(mode_!=NetworkMode::Client){world_.update(dt);if(mode_==NetworkMode::Host&&connected_){snapshotTimer_-=dt;if(snapshotTimer_<=0){snapshotTimer_=.05f;sendSnapshot();}}}
        for(auto&f:effects_){f.life-=dt;f.y-=24*dt;}effects_.erase(std::remove_if(effects_.begin(),effects_.end(),[](auto&f){return f.life<=0;}),effects_.end());
    }

    void send(const std::string&s){if(connected_&&peer_&&socket_.isOpen())socket_.send(peer_,s.data(),s.size());}
    void sendMove(float x,float y){std::ostringstream o;o<<"M "<<std::fixed<<std::setprecision(2)<<x<<' '<<y;for(auto&u:world_.units())if(u.selected&&u.team==1)o<<' '<<u.id;send(o.str());}
    void pumpNetwork(){
        if(mode_==NetworkMode::Offline||!socket_.isOpen())return;char b[65507];ra::net::Endpoint f;int n;
        while((n=socket_.receive(b,sizeof(b),f))>0){std::string m(b,b+n);if(mode_==NetworkMode::Host)hostPacket(m,f);else clientPacket(m,f);}
    }
    bool samePeer(const ra::net::Endpoint&f)const{return peer_&&f.address==peer_.address&&f.port==peer_.port;}
    void hostPacket(const std::string&m,const ra::net::Endpoint&f){
        if(!samePeer(f))return;if(m=="C"){++resources_;return;}if(m=="B"){buy(1);return;}
        if(m.rfind("M ",0)==0){std::istringstream in(m.substr(2));float x,y;in>>x>>y;std::vector<ra::rts::UnitId>ids;ra::rts::UnitId id;while(in>>id){auto*u=world_.find(id);if(u&&u->team==1)ids.push_back(id);}world_.commandMoveUnits(ids,{x,y},34);}
    }
    void sendSnapshot(){
        std::ostringstream o;o<<"S "<<resources_<<' '<<world_.units().size()<<std::fixed<<std::setprecision(2);
        for(auto&u:world_.units())o<<' '<<u.id<<' '<<u.position.x<<' '<<u.position.y<<' '<<u.destination.x<<' '<<u.destination.y<<' '<<u.home.x<<' '<<u.home.y<<' '<<u.speed<<' '<<u.team<<' '<<(u.autonomous?1:0)<<' '<<(int)u.order<<' '<<u.aiStep<<' '<<u.thinkCooldown<<' '<<u.patrolRadius<<' '<<u.health;send(o.str());
    }
    void clientPacket(const std::string&m,const ra::net::Endpoint&f){
        if(!samePeer(f)||m.rfind("S ",0)!=0)return;std::vector<ra::rts::UnitId>sel;for(auto&u:world_.units())if(u.selected)sel.push_back(u.id);
        std::istringstream in(m.substr(2));int count=0;if(!(in>>resources_>>count)||count<0||count>2048)return;std::vector<ra::rts::Unit>v;v.reserve(count);
        for(int i=0;i<count;++i){ra::rts::Unit u;int au=0,ord=0;if(!(in>>u.id>>u.position.x>>u.position.y>>u.destination.x>>u.destination.y>>u.home.x>>u.home.y>>u.speed>>u.team>>au>>ord>>u.aiStep>>u.thinkCooldown>>u.patrolRadius>>u.health))return;u.autonomous=au!=0;u.order=(ra::rts::UnitOrder)std::max(0,std::min(ord,2));u.maxHealth=100;u.radius=12;u.selected=u.team==1&&hasId(sel,u.id);v.push_back(u);}world_.replaceUnits(v);
    }

    void renderSplash(ra::Engine&e){float w=e.windowWidth(),h=e.windowHeight();e.clear({9,14,10,255});for(int y=0;y<12;++y)for(int x=0;x<22;++x)e.drawTexture(grass_,{w*.5f-352+x*32,h*.04f+y*32,32,32});e.drawTextureRegion(player_,frame48(0,0),{w*.5f-48,h*.28f,96,96});e.drawText(w*.5f-60,h*.46f,"CLICK COMMAND",{245,238,205,255});e.drawText(w*.5f-72,h*.50f,"MYSTIC WOODS RTS",{190,220,190,255});}
    void button(ra::Engine&e,float x,float y,const char*s){bool hot=inside(e.mouseX(),e.mouseY(),x,y,x+420,y+62);e.fillRect({x,y,420,62},hot?ra::Color{53,104,52,245}:ra::Color{29,63,34,245});e.drawRect({x,y,420,62},{125,167,121,255});e.drawText(x+150,y+27,s,{247,244,220,255});}
    void renderMenu(ra::Engine&e){float w=e.windowWidth(),h=e.windowHeight();e.clear({8,12,9,255});for(int y=0;y<18;++y)for(int x=0;x<30;++x){float dx=w*.5f-480+x*32,dy=h*.02f+y*32;e.drawTexture(grass_,{dx,dy,32,32});if((x*13+y*7)%31==0)e.drawTextureRegion(decor_,tile16((x+y)%4,0),{dx,dy,32,32});}e.fillRect({w*.5f-285,h*.29f,570,540},{10,16,12,225});e.drawText(w*.5f-60,h*.37f,"CLICK COMMAND",{245,238,205,255});e.drawText(w*.5f-80,h*.41f,"MYSTIC WOODS RTS + LAN",{190,220,190,255});float x=w*.5f-210,y=h*.52f;button(e,x,y,"SINGLE PLAYER");button(e,x,y+76,"HOST LAN GAME");button(e,x,y+152,"JOIN LAN GAME");}
    void renderLobby(ra::Engine&e){float w=e.windowWidth(),h=e.windowHeight();e.clear({9,14,10,255});e.fillRect({w*.5f-330,h*.3f,660,300},{14,22,16,255});e.drawRect({w*.5f-330,h*.3f,660,300},{125,167,121,255});e.drawText(w*.5f-50,h*.36f,mode_==NetworkMode::Host?"HOST LAN":"JOIN LAN",{245,238,205,255});e.drawText(w*.5f-150,h*.44f,lobbyStatus_,{190,220,190,255});e.drawText(w*.5f-135,h*.5f,"UDP 46000 - AUTO DISCOVERY",{150,175,155,255});}

    void renderGame(ra::Engine&e){float vw=hudX(e),vh=e.windowHeight();e.clear({8,13,9,255});drawTerrain(e,vw,vh);drawCamp(e,0,baseX_,baseY_);if(mode_!=NetworkMode::Offline)drawCamp(e,1,enemyX_,enemyY_);drawUnits(e,vw,vh);if(drag_){float l=std::min(sx_,ex_),r=std::max(sx_,ex_),t=std::min(sy_,ey_),b=std::max(sy_,ey_);e.drawRect({l,t,r-l,b-t},{96,238,104,255});}for(auto&f:effects_)e.drawText(f.x-4,f.y,"+1",{255,220,70,255});drawHud(e,vw,vh);}

    void drawDirtPatch(ra::Engine&e,int tx,int ty,int w,int h){
        for(int y=0;y<h;++y)for(int x=0;x<w;++x){int sx=(x==0?1:(x==w-1?3:2));int sy=(y==0?0:(y==h-1?2:1));float dx=(tx+x)*Tile,dy=(ty+y)*Tile;drawWorldTile(e,plains_,tile16(sx,sy),dx,dy);}
    }
    void drawWorldTile(ra::Engine&e,const ra::Texture&t,ra::Rect src,float wx,float wy){float s=Tile*zoom_;e.drawTextureRegion(t,src,{(wx-cameraX_)*zoom_,(wy-cameraY_)*zoom_,s,s});}
    void drawTerrain(ra::Engine&e,float vw,float vh){
        int fx=std::max(0,(int)std::floor(cameraX_/Tile)-1),fy=std::max(0,(int)std::floor(cameraY_/Tile)-1);int lx=std::min(MapW-1,fx+(int)(vw/(Tile*zoom_))+4),ly=std::min(MapH-1,fy+(int)(vh/(Tile*zoom_))+4);
        for(int y=fy;y<=ly;++y)for(int x=fx;x<=lx;++x){float s=Tile*zoom_,dx=(x*Tile-cameraX_)*zoom_,dy=(y*Tile-cameraY_)*zoom_;e.drawTexture(grass_,{dx,dy,s,s});int h=(x*31+y*47)%113;if(h==7||h==29||h==77)e.drawTextureRegion(decor_,tile16((x+y)%4,0),{dx,dy,s,s});}
        drawDirtPatch(e,19,16,15,9);drawDirtPatch(e,76,50,15,9);drawDirtPatch(e,34,29,28,3);drawDirtPatch(e,54,22,3,31);
        for(int yy=0;yy<3;++yy)for(int xx=0;xx<3;++xx)drawWorldTile(e,plains_,tile16(1+xx,4+yy),(10+xx)*Tile,(10+yy)*Tile);
        for(int yy=0;yy<3;++yy)for(int xx=0;xx<3;++xx)drawWorldTile(e,plains_,tile16(1+xx,4+yy),(104+xx)*Tile,(62+yy)*Tile);
    }

    void drawCamp(ra::Engine&e,int team,float wx,float wy){
        float cell=Tile;
        for(int y=-2;y<=2;++y)for(int x=-3;x<=3;++x)drawWorldTile(e,wooden_,tile16(0,0),wx+x*cell,wy+y*cell);
        for(int x=-3;x<=3;++x)drawWorldTile(e,flooring_,tile16((x+3)%5,1),wx+x*cell,wy+3*cell);
        for(int x=-2;x<=2;++x){drawWorldTile(e,walls_,tile16(1,0),wx+x*cell,wy-2*cell);drawWorldTile(e,walls_,tile16(1,3),wx+x*cell,wy+2*cell);}
        for(int y=-1;y<=1;++y){drawWorldTile(e,walls_,tile16(0,1),wx-3*cell,wy+y*cell);drawWorldTile(e,walls_,tile16(3,1),wx+3*cell,wy+y*cell);}
        for(int x=-4;x<=4;++x){drawWorldTile(e,fences_,tile16(1,1),wx+x*cell,wy-4*cell);drawWorldTile(e,fences_,tile16(1,1),wx+x*cell,wy+4*cell);}
        for(int y=-3;y<=3;++y){drawWorldTile(e,fences_,tile16(0,1),wx-5*cell,wy+y*cell);drawWorldTile(e,fences_,tile16(0,1),wx+5*cell,wy+y*cell);}
        float cx=(wx+2*cell-cameraX_)*zoom_,cy=(wy+cell-cameraY_)*zoom_;e.drawTextureRegion(chest_,tile16(0,0),{cx,cy,32*zoom_,32*zoom_});
        e.drawText((wx-cameraX_-52)*zoom_,(wy-cameraY_-150)*zoom_,team==0?"HUMAN CAMP":"SLIME CAMP",team==0?ra::Color{220,240,255,255}:ra::Color{180,230,180,255});
    }

    void drawUnits(ra::Engine&e,float vw,float vh){int i=0;for(auto&u:world_.units()){float x=(u.position.x-cameraX_)*zoom_,y=(u.position.y-cameraY_)*zoom_,s=(u.team?42.0f:52.0f)*zoom_;if(x<-s||y<-s||x>vw+s||y>vh+s){++i;continue;}if(u.selected)e.drawRect({x-s*.55f,y-s*.55f,s*1.1f,s*1.1f},{95,238,105,255});if(u.team==0)e.drawTextureRegion(player_,frame48((i+(int)u.aiStep)%6,0),{x-s*.5f,y-s*.72f,s,s});else e.drawTextureRegion(slime_,frame32((i+(int)u.aiStep)%4,0),{x-s*.5f,y-s*.5f,s,s});++i;}}
    void drawHud(ra::Engine&e,float x,float h){float pw=e.windowWidth()-x;e.fillRect({x,0,pw,h},{12,18,14,255});e.drawText(x+18,18,"CLICK COMMAND",{245,238,205,255});e.drawText(x+18,46,mode_==NetworkMode::Offline?"SINGLE PLAYER":(mode_==NetworkMode::Host?"LAN HOST":"LAN CLIENT"),{160,190,164,255});e.drawText(x+18,82,"RESOURCES",{160,180,164,255});e.drawText(x+18,106,std::to_string(resources_),{255,218,65,255});int cost=nextCost(localTeam());float bx=x+18,by=154,bw=pw-36;e.fillRect({bx,by,bw,76},resources_>=cost?ra::Color{43,92,51,255}:ra::Color{38,44,39,255});e.drawRect({bx,by,bw,76},{105,133,111,255});if(localTeam()==0)e.drawTextureRegion(player_,frame48(0,0),{bx+8,by+6,64,64});else e.drawTextureRegion(slime_,frame32(0,0),{bx+15,by+13,50,50});e.drawText(bx+82,by+20,localTeam()==0?"HUMAN UNIT":"SLIME UNIT",{243,242,218,255});e.drawText(bx+82,by+46,"COST: "+std::to_string(cost),{255,218,65,255});e.drawText(x+18,260,"UNITS: "+std::to_string(teamCount(localTeam())),{190,203,193,255});e.drawText(x+18,286,"SELECTED: "+std::to_string(world_.selectedCount()),{190,203,193,255});e.drawText(x+18,340,"LMB MAP  +1 / SELECT",{150,170,154,255});e.drawText(x+18,364,"RMB      MOVE",{150,170,154,255});e.drawText(x+18,388,"WASD     CAMERA",{150,170,154,255});e.drawText(x+18,412,"WHEEL    ZOOM",{150,170,154,255});}

    float hudX(const ra::Engine&e)const{return std::max(850.0f,(float)e.windowWidth()-310.0f);}
    float wX(float x)const{return cameraX_+x/zoom_;}float wY(float y)const{return cameraY_+y/zoom_;}
    void clampCamera(const ra::Engine&e){float vw=hudX(e)/zoom_,vh=e.windowHeight()/zoom_;cameraX_=clampf(cameraX_,0,std::max(0.0f,MapW*Tile-vw));cameraY_=clampf(cameraY_,0,std::max(0.0f,MapH*Tile-vh));}

    Screen screen_{Screen::Splash};NetworkMode mode_{NetworkMode::Offline};float splash_{};int resources_{};
    ra::rts::World world_{{0,0,MapW*Tile,MapH*Tile}};std::vector<ClickFx>effects_;float cameraX_{},cameraY_{},zoom_{1};bool drag_{};float sx_{},sy_{},ex_{},ey_{};
    const float baseX_=26*Tile,baseY_=20*Tile,enemyX_=86*Tile,enemyY_=54*Tile;
    ra::Texture grass_,plains_,decor_,fences_,walls_,flooring_,wooden_,chest_,player_,slime_;std::string assetError_;
    ra::net::DatagramSocket socket_;ra::net::Endpoint peer_{};bool connected_{};float discoverTimer_{},snapshotTimer_{};std::string lobbyStatus_;
};

int main(){ra::EngineConfig c;c.title="Click Command";c.width=1440;c.height=900;c.resizable=true;c.vsync=true;try{ra::Engine e(c);ClickerRTS g;return e.run(g);}catch(const std::exception&ex){std::ofstream out("RA-Framework-error.txt",std::ios::trunc);out<<ex.what()<<'\n';std::fprintf(stderr,"Fatal error: %s\n",ex.what());
#ifdef _WIN32
MessageBoxA(nullptr,ex.what(),"Click Command - Fatal Error",MB_OK|MB_ICONERROR);
#endif
return 1;}}
