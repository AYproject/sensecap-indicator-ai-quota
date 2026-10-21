// D1 AI Quota Monitor - actual online quota via a USB computer bridge.
#include "d1_display.h"
#include "quota_fonts.h"
#include "glass_assets.h"
#include <esp_heap_caps.h>
#include <stdlib.h>

class QuotaCanvas : public Arduino_Canvas {
public:
    QuotaCanvas() : Arduino_Canvas(480,480,gfx) {}
    bool begin(int32_t speed=GFX_SKIP_OUTPUT_BEGIN) override {
        _framebuffer=(uint16_t *)heap_caps_aligned_alloc(16,480*480*2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        return _framebuffer != nullptr;
    }
};
static QuotaCanvas screen;
static uint16_t rgb(uint32_t value) {
    return ((((value>>16)&255)*31+127)/255)<<11 |
           ((((value>>8)&255)*63+127)/255)<<5 | (((value&255)*31+127)/255);
}
static const uint16_t WHITE=rgb(0xeeeae2), MUTED=rgb(0xa7afb7), DIM=rgb(0x394750);
static const uint16_t ACCENT=rgb(0xb6ccd0), MINT=rgb(0xc5d3c9);
static constexpr uint32_t FRESH_SECONDS=900;
static constexpr uint32_t DEFAULT_IDLE_MS=120000;
static uint32_t sleepAfterMs=DEFAULT_IDLE_MS,lastInteractionMs=0,wakeCount=0;
static bool screenAsleep=false,wakeGesture=false;
struct Window {
    char label[17]="";
    float remaining=-1;
    uint32_t reset=0;
    char detail[37]="-";
};
struct Quota {
    uint32_t sequence=0, sent=0, sampled=0, receivedMs=0;
    char provider[17]="CODEX", plan[25]="CHATGPT PLAN", state[13]="waiting";
    int count=0;
    Window rows[3];
};
static Quota data;
static bool gotData=false, frozen=false, dirty=true;
static int focus=0;
static uint32_t frameCount=0,lastRenderMs=0,lastHealthMs=0;
static uint32_t renderMinute=0,renderAgeMinute=0;
static bool renderedLive=false,firstPresent=true;
static uint8_t renderedCurrentMask=0;
static uint32_t totalPixels=0,lastPixels=0;
static uint16_t outputRow[480]; // SRAM staging: never copy a full PSRAM canvas into active scanout.
static uint16_t *materialCache=nullptr;
static int cachedColumns=-1,cachedPillWidth=-1;
static bool cachedEmpty=false;
static char receiveBuf[1024];
static size_t receiveLen=0;
static bool receiveOverflow=false;

static uint16_t blend(uint16_t fg,uint16_t bg,uint8_t a) {
    int r=(((fg>>11)&31)*a+((bg>>11)&31)*(15-a)+7)/15;
    int g=(((fg>>5)&63)*a+((bg>>5)&63)*(15-a)+7)/15;
    int b=((fg&31)*a+(bg&31)*(15-a)+7)/15;
    return (r<<11)|(g<<5)|b;
}
static void alphaPixel(int x,int y,uint16_t ink,uint8_t alpha) {
    if (!alpha || x<0 || y<0 || x>=480 || y>=480) return;
    uint16_t bg=screen.getFramebuffer()[y*480+x];
    screen.drawPixel(x,y,alpha==15?ink:blend(ink,bg,alpha));
}
static int textWidth(const char *str,const QuotaFont *font) {
    int width=0;
    for (;*str;++str) if (*str>=32 && *str<=126) width+=font->glyphs[*str-32].advance;
    return width;
}
static void text(const char *str,int x,int baseline,const QuotaFont *font,uint16_t ink=WHITE) {
    for (;*str;++str) {
        if (*str<32 || *str>126) continue;
        const QuotaGlyph &glyph=font->glyphs[*str-32];
        for (int y=0;y<glyph.h;++y) for (int gx=0;gx<glyph.w;++gx) {
            int p=y*glyph.w+gx;
            uint8_t pair=font->pixels[glyph.offset+p/2];
            alphaPixel(x+glyph.x+gx,baseline+glyph.y+y,ink,(p&1)?pair&15:pair>>4);
        }
        x+=glyph.advance;
    }
}
static void rightText(const char *str,int right,int baseline,const QuotaFont *font,uint16_t ink=WHITE) {
    text(str,right-textWidth(str,font),baseline,font,ink);
}
static void roundedRect(int x,int y,int w,int h,int radius,uint16_t ink) {
    radius=min(radius,min(w,h)/2);
    if (radius<=0) { screen.fillRect(x,y,w,h,ink); return; }
    screen.fillRect(x+radius,y,w-radius*2,h,ink);
    screen.fillRect(x,y+radius,w,h-radius*2,ink);
    for (int dy=0;dy<radius;++dy) for (int dx=0;dx<radius;++dx) {
        float fx=radius-dx-0.5f,fy=radius-dy-0.5f;
        uint8_t a=(uint8_t)(constrain(radius+0.5f-sqrtf(fx*fx+fy*fy),0.0f,1.0f)*15+0.5f);
        alphaPixel(x+dx,y+dy,ink,a);
        alphaPixel(x+w-1-dx,y+dy,ink,a);
        alphaPixel(x+dx,y+h-1-dy,ink,a);
        alphaPixel(x+w-1-dx,y+h-1-dy,ink,a);
    }
}
static void circleStroke(int cx,int cy,int radius,uint16_t ink) {
    for (int y=-radius-1;y<=radius+1;++y) for (int x=-radius-1;x<=radius+1;++x) {
        if (x>2 && y<-1) continue; // An open refresh arc, without erasing the glass underneath.
        float coverage=constrain(1.25f-fabsf(sqrtf(x*x+y*y)-radius),0.0f,1.0f);
        alphaPixel(cx+x,cy+y,ink,(uint8_t)(coverage*15+0.5f));
    }
}
static void glassRect(int x,int y,int w,int h,int radius) {
    for (int py=0;py<h;++py) for (int px=0;px<w;++px) {
        float qx=fabsf(px+0.5f-w*0.5f)-(w*0.5f-radius);
        float qy=fabsf(py+0.5f-h*0.5f)-(h*0.5f-radius);
        float ox=max(qx,0.0f),oy=max(qy,0.0f);
        float distance=(ox>0 || oy>0?sqrtf(ox*ox+oy*oy):0.0f)+min(max(qx,qy),0.0f)-radius;
        uint8_t coverage=(uint8_t)(constrain(0.5f-distance,0.0f,1.0f)*15+0.5f);
        if (!coverage) continue;
        uint16_t ink=GlassMaterial[(y+py)*480+x+px];
        float edge=constrain(1.5f+distance,0.0f,1.0f);
        uint8_t rim=(uint8_t)((0.085f-0.060f*py/max(1,h-1))*edge*15+0.5f);
        if (rim) ink=blend(WHITE,ink,rim);
        alphaPixel(x+px,y+py,ink,coverage);
    }
}
static void prepareMaterial(int columns,int pillWidth,bool empty) {
    if (cachedColumns!=columns || cachedPillWidth!=pillWidth || cachedEmpty!=empty) {
        screen.draw16bitRGBBitmap(0,0,(uint16_t *)GlassBackdrop,480,480);
        glassRect(460-pillWidth,24,pillWidth,28,14);
        glassRect(20,86,440,212,30);
        if (empty) glassRect(20,310,440,120,26);
        else {
            int tileWidth=(440-(columns-1)*12)/columns;
            for (int i=0;i<columns;++i) glassRect(20+i*(tileWidth+12),310,tileWidth,120,26);
        }
        glassRect(348,436,108,36,18);
        memcpy(materialCache,screen.getFramebuffer(),480*480*2);
        cachedColumns=columns; cachedPillWidth=pillWidth; cachedEmpty=empty;
    } else memcpy(screen.getFramebuffer(),materialCache,480*480*2);
}
static const char *windowTitle(const char *label) {
    if (!strcmp(label,"5H")) return "5-hour limit";
    if (!strcmp(label,"WEEKLY")) return "Weekly limit";
    if (!strcmp(label,"MONTHLY")) return "Monthly limit";
    return label;
}
static const char *windowCaption(const char *label) {
    if (!strcmp(label,"5H")) return "5-hour window";
    if (!strcmp(label,"WEEKLY")) return "Weekly window";
    if (!strcmp(label,"MONTHLY")) return "Monthly window";
    return "Current window";
}
static const char *shortWindowTitle(const char *label) {
    if (!strcmp(label,"5H")) return "5-hour";
    if (!strcmp(label,"WEEKLY")) return "Weekly";
    if (!strcmp(label,"MONTHLY")) return "Monthly";
    return label;
}
static void centeredText(const char *label,int center,int baseline,const QuotaFont *font,uint16_t ink=WHITE) {
    text(label,center-textWidth(label,font)/2,baseline,font,ink);
}
static void heroValue(const char *value,bool known) {
    int numberWidth=textWidth(value,&QuotaSans100);
    int totalWidth=numberWidth+(known?textWidth("%",&QuotaSans32)+5:0);
    int left=240-totalWidth/2;
    text(value,left-2,226,&QuotaSans100);
    if (known) text("%",left+numberWidth+5,225,&QuotaSans32);
}
static const char *displayProvider() { return !strcmp(data.provider,"CODEX")?"ChatGPT":data.provider; }
static const char *planTitle() {
    if (!strcmp(data.plan,"TEAM")) return "Team";
    if (!strcmp(data.plan,"PLUS")) return "Plus";
    if (!strcmp(data.plan,"PRO")) return "Pro";
    if (!strcmp(data.plan,"FREE")) return "Free";
    if (!strcmp(data.plan,"CHATGPT PLAN")) return "Account";
    return data.plan;
}
static uint32_t currentEpoch() { return data.sent+(millis()-data.receivedMs)/1000; }
static uint32_t sourceAge() {
    uint32_t now=currentEpoch();
    if (data.sampled>now+30) return FRESH_SECONDS+1;
    return data.sampled && now>=data.sampled ? now-data.sampled : 0;
}
static bool isLive() {
    return gotData && strcmp(data.state,"live")==0 && data.sampled>0 && sourceAge()<=FRESH_SECONDS;
}
static bool rowIsCurrent(const Window &row) {
    return isLive() && row.remaining>=0 && (!row.reset || row.reset>currentEpoch());
}
static uint8_t currentWindowMask() {
    uint8_t mask=0;
    for (int i=0;i<data.count;++i) if (rowIsCurrent(data.rows[i])) mask|=1<<i;
    return mask;
}
static const char *unavailableLabel() {
    if (!strcmp(data.state,"sync")) return "Syncing account...";
    if (!strcmp(data.state,"auth")) return "Sign in on PC";
    if (!strcmp(data.state,"error")) return "Update unavailable";
    if (!gotData) return "Waiting for account";
    return "Awaiting fresh data";
}
static void countdown(uint32_t reset,char *out,size_t capacity) {
    if (!reset) { snprintf(out,capacity,"Reset time unavailable"); return; }
    int64_t seconds=(int64_t)reset-currentEpoch();
    if (seconds<=0) { snprintf(out,capacity,"Reset update pending"); return; }
    uint32_t minutes=(seconds+59)/60;
    if (minutes>=1440) snprintf(out,capacity,"%lud %02luh",(unsigned long)(minutes/1440),(unsigned long)((minutes%1440)/60));
    else snprintf(out,capacity,"%luh %02lum",(unsigned long)(minutes/60),(unsigned long)(minutes%60));
}
static void presentChanges() {
    const uint16_t *composed=screen.getFramebuffer();
    const uint16_t *visible=gfx->getFramebuffer();
    lastPixels=0;
    for (int y=0;y<480;++y) {
        const uint16_t *row=composed+y*480, *old=visible+y*480;
        if (!firstPresent && memcmp(row,old,480*2)==0) continue;
        int left=0,right=479;
        if (!firstPresent) {
            while (left<480 && row[left]==old[left]) ++left;
            while (right>left && row[right]==old[right]) --right;
        }
        int width=right-left+1;
        memcpy(outputRow,row+left,width*2);
        gfx->draw16bitRGBBitmap(left,y,outputRow,width,1);
        lastPixels+=width;
        if ((y&15)==15) delay(1); // Give the LCD refill task bandwidth during initial/large view changes.
    }
    firstPresent=false;
    if (lastPixels) { ++frameCount; totalPixels+=lastPixels; }
}
static void render() {
    const char *plan=planTitle();
    int pillWidth=min(148,max(62,textWidth(plan,&QuotaSans13)+28));
    prepareMaterial(max(2,data.count),pillWidth,!gotData || data.count==0);
    text(displayProvider(),28,44,&QuotaSans24);
    text(!strcmp(data.provider,"CODEX")?"Codex usage":"Account overview",29,66,&QuotaSans13,MUTED);
    if (textWidth(plan,&QuotaSans13)<=pillWidth-20)
        text(plan,460-pillWidth+(pillWidth-textWidth(plan,&QuotaSans13))/2,43,&QuotaSans13,MUTED);
    else text("Account",460-pillWidth+14,43,&QuotaSans13,MUTED);
    char value[24],reset[50];
    if (!gotData || data.count==0) {
        text("Available quota",42,121,&QuotaSans14,MUTED);
        rightText("-- / --",438,121,&QuotaSans13,MUTED);
        heroValue("--",false);
        centeredText(unavailableLabel(),240,252,&QuotaSans14,MUTED);
        roundedRect(42,275,396,4,2,DIM);
        text(!strcmp(data.state,"sync")?"Getting latest quota":"Account connection",40,350,&QuotaSans18);
        text(!strcmp(data.state,"auth")?"Open Codex on the PC and sign in.":"The PC sync service will retry automatically.",40,390,&QuotaSans14,MUTED);
    } else {
        focus=constrain(focus,0,data.count-1);
        Window &w=data.rows[focus];
        text(windowCaption(w.label),42,121,&QuotaSans14,MUTED);
        char page[16];
        snprintf(page,sizeof(page),"%02d / %02d",focus+1,data.count);
        rightText(page,438,121,&QuotaSans13,MUTED);
        bool current=rowIsCurrent(w);
        if (!current) snprintf(value,sizeof(value),"--");
        else snprintf(value,sizeof(value),"%.0f",w.remaining);
        heroValue(value,current);
        centeredText(current?"remaining":unavailableLabel(),240,252,&QuotaSans14,MUTED);
        roundedRect(42,275,396,4,2,DIM);
        if (current && w.remaining>0) roundedRect(42,275,max(1,(int)(396*w.remaining/100)),4,2,ACCENT);
        int columns=max(2,data.count);
        int tileWidth=(440-(columns-1)*12)/columns;
        text(strcmp(w.detail,"-")==0?"Resets in":"Balance",40,342,&QuotaSans14,MUTED);
        if (current) countdown(w.reset,reset,sizeof(reset));
        else snprintf(reset,sizeof(reset),"--");
        const char *detail=current && strcmp(w.detail,"-")!=0?w.detail:reset;
        const QuotaFont *resetFont=columns==3?&QuotaSans18:&QuotaSans32;
        if (textWidth(detail,resetFont)>tileWidth-40) resetFont=&QuotaSans14;
        text(detail,40,384,resetFont);
        text(windowCaption(w.label),40,411,&QuotaSans13,MUTED);
        int displayed=0;
        for (int i=0;i<data.count;++i) {
            if (i==focus) continue;
            Window &row=data.rows[i];
            int x=20+(++displayed)*(tileWidth+12)+20;
            text(shortWindowTitle(row.label),x,342,columns==3?&QuotaSans13:&QuotaSans14,MUTED);
            bool rowCurrent=rowIsCurrent(row);
            if (!rowCurrent) snprintf(value,sizeof(value),"--");
            else snprintf(value,sizeof(value),"%.0f%%",row.remaining);
            text(value,x,384,columns==3?&QuotaSans24:&QuotaSans32,MINT);
            if (rowCurrent) countdown(row.reset,reset,sizeof(reset));
            else snprintf(reset,sizeof(reset),"Not synced");
            char note[72];
            if (rowCurrent) snprintf(note,sizeof(note),columns==3?"%s to reset":"Resets in %s",reset);
            else snprintf(note,sizeof(note),"Waiting for update");
            text(rowCurrent && strcmp(row.detail,"-")!=0?row.detail:note,x,411,&QuotaSans13,MUTED);
        }
        if (data.count==1) {
            text("Source",272,342,&QuotaSans14,MUTED);
            text(displayProvider(),272,383,&QuotaSans18);
            text("Online account",272,411,&QuotaSans13,MUTED);
        }
    }
    char footer[64];
    if (frozen) snprintf(footer,sizeof(footer),"Photo mode");
    else if (!gotData) snprintf(footer,sizeof(footer),"Waiting for connection");
    else if (!strcmp(data.state,"sync")) snprintf(footer,sizeof(footer),"Checking online quota...");
    else if (!strcmp(data.state,"auth")) snprintf(footer,sizeof(footer),"Account sign-in required");
    else if (!strcmp(data.state,"error")) snprintf(footer,sizeof(footer),"Update failed / retrying");
    else if (!data.sampled) snprintf(footer,sizeof(footer),"Account unavailable");
    else if (!isLive()) snprintf(footer,sizeof(footer),"%s / %lum old",strcmp(data.state,"live")==0?"Offline":"Source unavailable",(unsigned long)(sourceAge()/60));
    else if (sourceAge()<60) snprintf(footer,sizeof(footer),"Updated just now");
    else snprintf(footer,sizeof(footer),"Updated %lum ago",(unsigned long)(sourceAge()/60));
    screen.fillCircle(30,454,3,isLive()?MINT:MUTED);
    text(footer,42,459,&QuotaSans13,MUTED);
    if (frozen) {
        screen.fillTriangle(369,448,369,460,377,454,WHITE);
        text("Resume",389,459,&QuotaSans13);
    } else {
        circleStroke(373,454,6,WHITE);
        screen.fillTriangle(379,447,373,449,379,453,WHITE);
        text("Refresh",389,459,&QuotaSans13);
    }
    presentChanges();
    lastRenderMs=millis();
    renderMinute=currentEpoch()/60;
    renderAgeMinute=sourceAge()/60;
    renderedLive=isLive();
    renderedCurrentMask=currentWindowMask();
    dirty=false;
}

static bool uintField(const char *s,uint32_t &value) {
    if (!s || !*s || *s=='-') return false;
    char *end=nullptr; unsigned long v=strtoul(s,&end,10);
    if (!end || *end) return false;
    value=v; return true;
}
static void handlePacket(char *line) {
    char *fields[22],*save=nullptr;
    int count=0;
    for (char *part=strtok_r(line,"|",&save);part && count<22;part=strtok_r(nullptr,"|",&save)) fields[count++]=part;
    if (count<8 || strcmp(fields[0],"Q1")!=0) { Serial.println("REJECT packet_header"); return; }
    Quota next;
    uint32_t rows;
    if (!uintField(fields[1],next.sequence) || !uintField(fields[2],next.sent) ||
        !uintField(fields[3],next.sampled) || !uintField(fields[7],rows) || rows>3 || count!=8+(int)rows*4) {
        Serial.println("REJECT packet_fields"); return;
    }
    if (strcmp(fields[6],"live") && strcmp(fields[6],"error") && strcmp(fields[6],"auth") && strcmp(fields[6],"empty") && strcmp(fields[6],"sync")) {
        Serial.println("REJECT packet_state"); return;
    }
    snprintf(next.provider,sizeof(next.provider),"%s",fields[4]);
    snprintf(next.plan,sizeof(next.plan),"%s",fields[5]);
    snprintf(next.state,sizeof(next.state),"%s",fields[6]);
    next.count=rows;
    for (int i=0;i<(int)rows;++i) {
        int base=8+i*4;
        char *end=nullptr;
        float value=strtof(fields[base+1],&end);
        if (!end || *end || !isfinite(value) || value < -1 || value > 100 || !uintField(fields[base+2],next.rows[i].reset)) {
            Serial.println("REJECT packet_value"); return;
        }
        snprintf(next.rows[i].label,sizeof(next.rows[i].label),"%s",fields[base]);
        next.rows[i].remaining=value;
        snprintf(next.rows[i].detail,sizeof(next.rows[i].detail),"%s",fields[base+3]);
    }
    next.receivedMs=millis();
    data=next;
    gotData=true;
    dirty=true;
    Serial.printf("ACK seq=%lu provider=%s rows=%d state=%s\n",(unsigned long)data.sequence,data.provider,data.count,data.state);
}
static void dumpFrame() {
    const uint16_t *pixels=screen.getFramebuffer();
    Serial.println("FRAME_RLE 480 480");
    for (int i=0;i<480*480;) {
        uint16_t value=pixels[i],run=1;
        while (i+run<480*480 && run<65535 && pixels[i+run]==value) ++run;
        uint8_t bytes[]={(uint8_t)(run>>8),(uint8_t)run,(uint8_t)(value>>8),(uint8_t)value};
        Serial.write(bytes,4); i+=run;
    }
    Serial.println("\nFRAME_END");
}
// Keep USB, touch and quota reception running; only turn off the LCD backlight.
static void sleepScreen() {
    if (screenAsleep) return;
    digitalWrite(BACKLIGHT_PIN,LOW);
    screenAsleep=true;
    Serial.println("SCREEN asleep=1");
}
static void wakeScreen() {
    lastInteractionMs=millis();
    if (!screenAsleep) return;
    screenAsleep=false;
    // Compose current values (including expiry) before exposing the display.
    if (!frozen) render();
    delay(20);
    digitalWrite(BACKLIGHT_PIN,HIGH);
    ++wakeCount;
    Serial.println("SCREEN asleep=0");
}
static void handleLine(char *line) {
    if (strncmp(line,"Q1|",3)==0) handlePacket(line);
    else if (strcmp(line,"STATUS")==0) Serial.printf("STATUS seq=%lu rows=%d remaining=%.3f frozen=%u frames=%lu age=%lu live=%u psram=%u pixels=%lu last_pixels=%lu incremental=1 fresh_for=%lu ui=studio_v052 material=cached_frosted aa=4 title=%s visible_mask=%u state=%s sampled=%lu asleep=%u backlight=%u idle_ms=%lu sleep_after_ms=%lu focus=%d wakes=%lu\n",
        (unsigned long)data.sequence,data.count,data.count && rowIsCurrent(data.rows[focus])?data.rows[focus].remaining:-1,frozen,
        (unsigned long)frameCount,(unsigned long)sourceAge(),isLive(),ESP.getFreePsram(),(unsigned long)totalPixels,(unsigned long)lastPixels,(unsigned long)FRESH_SECONDS,displayProvider(),currentWindowMask(),data.state,(unsigned long)data.sampled,
        screenAsleep,digitalRead(BACKLIGHT_PIN)==HIGH,(unsigned long)(millis()-lastInteractionMs),(unsigned long)sleepAfterMs,focus,(unsigned long)wakeCount);
    else if (strcmp(line,"SCREEN OFF")==0) sleepScreen();
    else if (strcmp(line,"SCREEN ON")==0) wakeScreen();
    else if (strncmp(line,"IDLE_MS ",8)==0) {
        uint32_t duration;
        // Volatile diagnostic setting; every boot restores the two-minute default.
        if (uintField(line+8,duration) && duration>=1000 && duration<=3600000) {
            sleepAfterMs=duration; lastInteractionMs=millis();
            Serial.printf("IDLE_MS %lu\n",(unsigned long)sleepAfterMs);
        } else Serial.println("REJECT idle_duration");
    }
    else if (strcmp(line,"REDRAW")==0) { render(); Serial.println("REDRAW done"); }
    else if (strcmp(line,"DUMP")==0) { if (!frozen && dirty) render(); dumpFrame(); }
    else if (strcmp(line,"FREEZE")==0) { frozen=!frozen; dirty=true; render(); Serial.printf("PHOTO frozen=%u\n",frozen); }
    else if (strcmp(line,"NEXT")==0 && data.count) { focus=(focus+1)%data.count; dirty=true; }
}
static void serialInput() {
    while (Serial.available()) {
        char c=Serial.read();
        if (c=='\r') continue;
        if (c=='\n') {
            if (!receiveOverflow && receiveLen) { receiveBuf[receiveLen]='\0'; handleLine(receiveBuf); }
            else if (receiveOverflow) Serial.println("REJECT packet_overflow");
            receiveLen=0; receiveOverflow=false;
        } else if (!receiveOverflow) {
            if (receiveLen+1<sizeof(receiveBuf)) receiveBuf[receiveLen++]=c;
            else receiveOverflow=true;
        }
    }
}
static bool touchingBefore=false,holdUsed=false;
static int touchX=0,touchY=0,lastX=0,lastY=0;
static uint32_t touchBegan=0,lastRefreshRequest=0;
static void touchInput() {
    bool down=touch.read();
    uint32_t now=millis();
    if (down) {
        lastInteractionMs=now;
        if (screenAsleep) { wakeGesture=true; wakeScreen(); }
    }
    // Consume the entire first gesture, even a swipe or a long press, through release.
    if (wakeGesture) {
        if (!down) { wakeGesture=false; lastInteractionMs=now; }
        touchingBefore=false;
        return;
    }
    if (!down && touchingBefore) lastInteractionMs=now;
    if (down) {
        TP_Point p=touch.getPoint(0);
        int x=constrain(479-(int)p.x,0,479),y=constrain(479-(int)p.y,0,479);
        lastX=x; lastY=y;
        if (!touchingBefore) { touchX=x; touchY=y; touchBegan=now; holdUsed=false; }
        else if (!holdUsed && now-touchBegan>=1200 && abs(x-touchX)<40 && abs(y-touchY)<40) {
            frozen=!frozen; holdUsed=true; dirty=true; render();
            Serial.printf("PHOTO frozen=%u\n",frozen);
        }
    } else if (touchingBefore && !holdUsed) {
        if (frozen) { frozen=false; dirty=true; }
        else if (abs(lastX-touchX)>60) {
            if (data.count) { focus=(focus+1)%data.count; dirty=true; }
        } else if (abs(lastX-touchX)<40 && abs(lastY-touchY)<40) {
            if (touchY>=435 && touchX>=338 && now-lastRefreshRequest>=3000) {
                Serial.println("REQUEST REFRESH"); lastRefreshRequest=now;
            } else if (touchY>=86 && touchY<298 && data.count) {
                focus=(focus+1)%data.count; dirty=true;
            } else if (touchY>=310 && touchY<430 && data.count>1) {
                int columns=max(2,data.count),tileWidth=(440-(columns-1)*12)/columns;
                int column=(touchX-20)/(tileWidth+12);
                int displayed=0;
                for (int i=0;i<data.count;++i) if (i!=focus && ++displayed==column) {
                    focus=i; dirty=true; break;
                }
            }
        }
    }
    touchingBefore=down;
}
void setup() {
    Serial.begin(115200); delay(200);
    Serial.println("D1_AI_QUOTA v0.5.2 - idle screen off, touch to wake");
    indicator_hw_init();
    if (!gfx->begin(12000000L)) { Serial.println("ERROR display_init"); while (true) delay(1000); }
    pinMode(BACKLIGHT_PIN,OUTPUT); digitalWrite(BACKLIGHT_PIN,HIGH);
    if (!screen.begin()) { Serial.println("ERROR psram_canvas"); while (true) delay(1000); }
    materialCache=(uint16_t *)heap_caps_aligned_alloc(16,480*480*2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if (!materialCache) { Serial.println("ERROR psram_material"); while (true) delay(1000); }
    Wire.beginTransmission(TOUCH_MODULE_ADDR);
    uint8_t probe=Wire.endTransmission();
    Serial.printf("TOUCH probe=%s\n",probe==0?"OK":"FAILED");
    touch.init();
    render();
    lastInteractionMs=millis();
    Serial.printf("READY D1_AI_QUOTA 480x480 free_psram=%u\n",ESP.getFreePsram());
}
void loop() {
    serialInput(); touchInput();
    uint32_t now=millis();
    // Quotas do not need a ticking seconds display. Recompose only when visible state can change.
    if (!screenAsleep && !touchingBefore && !wakeGesture && now-lastInteractionMs>=sleepAfterMs) sleepScreen();
    if (!screenAsleep && !frozen && (dirty || currentEpoch()/60!=renderMinute || sourceAge()/60!=renderAgeMinute || isLive()!=renderedLive || currentWindowMask()!=renderedCurrentMask)) render();
    if (now-lastHealthMs>=30000) {
        Serial.printf("HEALTH frames=%lu free_heap=%u free_psram=%u\n",(unsigned long)frameCount,ESP.getFreeHeap(),ESP.getFreePsram());
        lastHealthMs=now;
    }
    delay(2);
}
