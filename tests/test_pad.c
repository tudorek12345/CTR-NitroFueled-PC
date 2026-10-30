#define _GNU_SOURCE
#include <assert.h>
#include <unistd.h>
#include "../src/runtime_pad.c"

static int capture;
static uint32_t mouse_state;
static int user_login[PORTS];
uint32_t bbgpu_pc_input_state(void) { return mouse_state; }
void runtime_services_user_change(int user, int login) { user_login[user-1]=login; }
int bbgpu_overlay_captures_input(void) { return capture; }
int bbgpu_pad_rescan_take(void) { return 0; }
uintptr_t runtime_lookup(const RuntimeExport *table, size_t count, const char *name) {
    (void)table; (void)count; (void)name;
    return 0;
}

static int temp_file(char *path, size_t size, const char *name) {
#ifdef _WIN32
    const char *temp=getenv("TEMP");
    assert(temp);
    snprintf(path,size,"%s/%s-XXXXXX",temp,name);
    assert(_mktemp_s(path,size)==0);
    return _open(path,_O_CREAT|_O_EXCL|_O_RDWR|_O_BINARY,_S_IREAD|_S_IWRITE);
#else
    snprintf(path,size,"/tmp/%s-XXXXXX",name);
    return mkstemp(path);
#endif
}

static void test_env(const char *name, const char *value) {
#ifdef _WIN32
    assert(_putenv_s(name,value)==0);
#else
    assert(setenv(name,value,1)==0);
#endif
}

static void inject(const char *path, const char *tokens) {
    FILE *f=fopen(path,"w");
    assert(f);
    fputs(tokens,f);
    fclose(f);
    SDL_Delay(25);
}

int main(void) {
#ifdef _WIN32
    _set_error_mode(_OUT_TO_STDERR); /* assertions must reach the build log, not a GUI dialog */
#endif
    char path[1024];
    int fd=temp_file(path,sizeof path,"bbport-pad-test");
    assert(fd>=0);
    close(fd);
    test_env("BB_PAD_FILE",path);
    /* bbport.ini controls: buttons moved, a trigger as a button and a button as a trigger. */
    char config[1024];
    int config_fd=temp_file(config,sizeof config,"bbport-pad-config");
    assert(config_fd>=0);
    const char controls[]="upscaler=fsr3\npad.cross=b\npad.circle=a\npad.r2=rightshoulder\n"
                          "pad.r1=righttrigger\nkey.cross=X, Space\npad.bogus=a\n";
    assert(write(config_fd,controls,sizeof(controls)-1)==(ssize_t)(sizeof(controls)-1));
    close(config_fd);
    test_env("BB_CONFIG",config);
    test_env("SDL_VIDEODRIVER","dummy");
    /* Only the virtual test controller is a gamepad, whatever is plugged in. */
    SDL_SetHint(SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT,"0x1d50/0x6189");
    assert(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMEPAD));
    assert(pad_init()==0 && pad_open(1,0,0,NULL)==1);
    PadData data;
    inject(path,"cross l3 touchpad_left");
    assert(pad_read_state(1,&data)==0);
    assert((data.buttons & (BTN_CROSS|BTN_L3|BTN_TOUCHPAD))==(BTN_CROSS|BTN_L3|BTN_TOUCHPAD));
    /* A new touch gets a new id (1..127), as from a DualShock 4: the game ignores id 0. */
    assert(data.touch_count==1 && data.touches[0].x==480 && data.touches[0].y==471 && data.touches[0].id==1);
    inject(path,"touchpad_right");
    assert(pad_read_state(1,&data)==0 && data.touch_count==1 && data.touches[0].x==1440);
    inject(path,"");
    assert(pad_read_state(1,&data)==0 && data.buttons==0 && data.touch_count==0);

    SDL_VirtualJoystickTouchpadDesc touch={.nfingers=2};
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type=SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes=SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons=SDL_GAMEPAD_BUTTON_COUNT;
    desc.button_mask=(1u<<SDL_GAMEPAD_BUTTON_COUNT)-1;
    desc.axis_mask=(1u<<SDL_GAMEPAD_AXIS_COUNT)-1;
    desc.name="bbport test controller";
    desc.vendor_id=0x1d50;
    desc.product_id=0x6189;
    desc.ntouchpads=1;
    desc.touchpads=&touch;
    SDL_JoystickID id=SDL_AttachVirtualJoystick(&desc);
    assert(id!=0);
    SDL_Joystick *joystick=SDL_OpenJoystick(id);
    assert(joystick);
    assert(SDL_SetJoystickVirtualTouchpad(joystick,0,0,true,0.75f,0.5f,1.0f));
    assert(SDL_SetJoystickVirtualTouchpad(joystick,0,1,true,0.25f,1.0f,1.0f));
    assert(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_TOUCHPAD,true));
    SDL_UpdateJoysticks();
    SDL_UpdateGamepads();
    assert(pad_read_state(1,&data)==0);
    assert(gamepad && data.touch_count==2 && (data.buttons & BTN_TOUCHPAD));
    assert(data.touches[0].x==1439 && data.touches[0].y==471 && data.touches[0].id==2);
    assert(data.touches[1].x==480 && data.touches[1].y==942 && data.touches[1].id==3);
    capture=1;
    assert(pad_read_state(1,&data)==0 && data.touch_count==0 && data.buttons==0);
    capture=0;
    assert(SDL_SetJoystickVirtualTouchpad(joystick,0,0,false,0,0,0));
    assert(SDL_SetJoystickVirtualTouchpad(joystick,0,1,false,0,0,0));
    SDL_UpdateJoysticks();
    SDL_UpdateGamepads();
    assert(pad_read_state(1,&data)==0 && data.touch_count==1 && data.touches[0].x==480);
    assert(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_TOUCHPAD,false));
    assert(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_EAST,true));
    assert(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,true));
    assert(SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,32767));
    SDL_UpdateJoysticks();
    SDL_UpdateGamepads();
    assert(pad_read_state(1,&data)==0);
    assert(data.buttons==(BTN_CROSS|BTN_R2|BTN_R1) && data.r2==255);
    assert(bindings[IN_CROSS].key_count==2 && bindings[IN_CROSS].keys[0]==SDL_SCANCODE_X &&
           bindings[IN_CROSS].keys[1]==SDL_SCANCODE_SPACE);

    /* Switch a controller already active on P1 to P2 without restarting. */
    FILE *split=fopen(config,"w");
    assert(split);
    char guid[33];
    SDL_GUIDToString(SDL_GetGamepadGUIDForID(id),guid,sizeof guid);
    fprintf(split,"port.1=keyboard_mouse\nport.2=%s\n",guid);
    fclose(split);
    config_checked=0;
    assert(pad_read_state(1,&data)==0);
    assert(gamepad==NULL && data.buttons==0 && data.left_x==128);
    update_port_presence();
    assert(user_login[1] && ports[1].pad && runtime_pad_user_present(2));
    assert(pad_open(2,0,0,NULL)==2);
    SDL_UpdateGamepads();
    PadData second;
    assert(pad_read_state(2,&second)==0);
    assert(second.buttons==(BTN_CROSS|BTN_R2|BTN_R1) && second.r2==255);

    /* Mouse aliases affect only P1 and never change steering, even while P2 holds inputs. */
    mouse_state=SDL_BUTTON_LMASK|SDL_BUTTON_RMASK|SDL_BUTTON_MMASK|SDL_BUTTON_X1MASK|SDL_BUTTON_X2MASK;
    assert(pad_read_state(1,&data)==0);
    assert(data.buttons==(BTN_CROSS|BTN_SQUARE|BTN_CIRCLE|BTN_L1|BTN_R1));
    assert(data.left_x==128 && data.left_y==128 && data.right_x==128 && data.right_y==128);
    assert(pad_read_state(2,&second)==0 && second.buttons==(BTN_CROSS|BTN_R2|BTN_R1));
    bool keys[SDL_SCANCODE_COUNT]={false};
    keys[SDL_SCANCODE_A]=true;
    apply_keyboard(&data,keys);
    assert(data.left_x==0 && (data.buttons & BTN_CROSS));
    mouse_state=0;
    assert(pad_read_state(1,&data)==0 && data.buttons==0);
    mouse_state=BBGPU_PC_INPUT_INACTIVE|SDL_BUTTON_LMASK;
    assert(pad_read_state(1,&data)==0 && data.buttons==0 && data.left_x==128);
    mouse_state=SDL_BUTTON_LMASK;
    capture=1;
    assert(pad_read_state(1,&data)==0 && data.buttons==0);
    assert(pad_read_state(2,&second)==0 && second.buttons==0);
    capture=0; mouse_state=0;
    /* PC racing preset: W gas, S brake/reverse, independent drift/boost, remapped mouse. */
    split=fopen(config,"a");
    assert(split);
    fputs("key.cross=W,Return\nkey.square=S\nkey.circle=E,Backspace\nkey.r1=Space\nkey.l1=Left Shift\n"
          "key.move_up=\nkey.move_down=S\nmouse.cross=\nmouse.square=\nmouse.circle=left\n"
          "mouse.r1=right\nmouse.l1=middle\nmouse.r2=x1\nmouse.l2=x2\n"
          "pad.cross=b\npad.circle=a\npad.r2=rightshoulder\npad.r1=righttrigger\n",split);
    fclose(split);
    load_bindings();
    mouse_state=SDL_BUTTON_LMASK|SDL_BUTTON_RMASK;
    assert(pad_read_state(1,&data)==0 && data.buttons==(BTN_CIRCLE|BTN_R1));
    memset(keys,0,sizeof keys);
    keys[SDL_SCANCODE_W]=keys[SDL_SCANCODE_D]=keys[SDL_SCANCODE_LSHIFT]=true;
    apply_keyboard(&data,keys);
    assert(data.buttons==(BTN_CIRCLE|BTN_R1|BTN_L1|BTN_CROSS));
    assert(data.left_x==255 && data.left_y==128);
    assert(pad_read_state(2,&second)==0 && second.buttons==(BTN_CROSS|BTN_R2|BTN_R1));
    mouse_state=SDL_BUTTON_X1MASK;
    assert(pad_read_state(1,&data)==0 && data.buttons==BTN_R2 && data.r2==255);
    memset(keys,0,sizeof keys);
    keys[SDL_SCANCODE_S]=true;
    apply_keyboard(&data,keys);
    assert((data.buttons & BTN_SQUARE) && data.left_y==255);
    mouse_state=0;
    SDL_CloseGamepad(ports[1].pad); ports[1].pad=NULL;
    SDL_CloseJoystick(joystick);
    if (gamepad) SDL_CloseGamepad(gamepad);
    gamepad=NULL;
    assert(SDL_DetachVirtualJoystick(id));
    update_port_presence();
    assert(!user_login[1] && !runtime_pad_user_present(2));
    /* An automatic port reconnects to an available controller without stealing P1. */
    split=fopen(config,"w");
    assert(split);
    fputs("port.1=keyboard_mouse\nport.2=auto\n",split);
    fclose(split);
    config_checked=0;
    id=SDL_AttachVirtualJoystick(&desc);
    assert(id && current_gamepad()==NULL);
    update_port_presence();
    assert(user_login[1] && ports[1].pad);
    SDL_CloseGamepad(ports[1].pad); ports[1].pad=NULL;
    assert(SDL_DetachVirtualJoystick(id));
    SDL_Quit();
    unlink(path);
    unlink(config);
    /* Stick calibration (bbport 0.5, PR 71; per port here): a genuine pad reads straight through
     * the 5/127 dead zone; a clone biased to +16380 on all four axes gets that as its neutral and
     * its half-span as the travel, so a full push still reaches full deflection. */
    {
        PadCal c; cal_reset(&c);
        assert(c.have_center==0 && cal_enabled==1 && pad_deadzone==5 && pad_deadzone_outer==127);
        int16_t zero[4]={0,0,0,0};
        for (int i=0;i<PAD_CAL_QUIET_SAMPLES+1;++i) cal_sample(&c,zero);
        assert(!c.have_center); /* SDL's pre-report zeros are not a neutral yet */
        c.since-=PAD_CAL_TIMEOUT_US+1;
        for (int i=0;i<PAD_CAL_QUIET_SAMPLES;++i) cal_sample(&c,zero);
        assert(c.have_center && c.center[0]==0 && c.base[0]==0);
        assert(stick_axis(&c,0,0)==128 && stick_axis(&c,0,32767)==255 && stick_axis(&c,0,-32768)==0);
        assert(stick_axis(&c,0,600)==128);                /* inside the dead zone */
        assert(stick_axis(&c,0,16384)>128+50 && stick_axis(&c,0,16384)<128+70);
        PadCal clone; cal_reset(&clone);
        int16_t biased[4]={16380,16380,16380,16380};
        for (int i=0;i<PAD_CAL_QUIET_SAMPLES;++i) cal_sample(&clone,biased);
        assert(clone.have_center && clone.center[0]==16380 && clone.base[0]==16380);
        assert(stick_axis(&clone,0,16380)==128 && stick_axis(&clone,0,32760)==255 && stick_axis(&clone,0,0)==0);
    }
    puts("PASS: pad ABI, touch, overlay capture, controls, calibration, keyboard/mouse P1, isolated P2, live reassignment, disconnect/reconnect");
}
