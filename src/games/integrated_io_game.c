#include "openconsole/audio_protocol.h"
#include "openconsole/gles2_protocol.h"
#include "openconsole/net_protocol.h"

#define FIXTURE_ADDRESS ((uint32_t)10u | ((uint32_t)2u << 16) | ((uint32_t)2u << 24))
#define MUSIC_FRAMES (OC_AUDIO_MIX_RATE * 4u)
#define EFFECT_FRAMES (OC_AUDIO_MIX_RATE / 4u)
#define EFI_EVENT_TIMER 0x80000000u
#define EFI_TPL_CALLBACK 8u
#define EFI_TIMER_PERIODIC 1u
#define EFI_LOCATE_BY_PROTOCOL 2u
#define RESOLVE(type, name) ((type)gles->get_proc_address(name))

static const EFI_GUID simple_pointer_guid =
    {0x31878c87u,0x0b75u,0x11d5u,{0x9a,0x4f,0x00,0x90,0x27,0x3f,0xc1,0x4d}};
static const EFI_GUID loaded_image_guid =
    {0x5b1b31a1u,0x9562u,0x11d2u,{0x8e,0x3f,0x00,0xa0,0xc9,0x69,0x72,0x3b}};
static const EFI_GUID simple_fs_guid =
    {0x964e5b22u,0x6459u,0x11d2u,{0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b}};

typedef struct {
    CHAR16 left, right, sound, exit;
} slice_controls;

static int16_t music_samples[MUSIC_FRAMES * 2u];
static int16_t effect_samples[EFFECT_FRAMES];
static int bytes_equal(const void *, const void *, UINTN);

static void debug_putc(char value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"((uint16_t)0x402));
}

static void debug_write(const char *text) {
    while (*text != '\0') debug_putc(*text++);
}

static void debug_hex64(uint64_t value) {
    static const char digits[] = "0123456789ABCDEF";
    int shift;
    for (shift = 60; shift >= 0; shift -= 4)
        debug_putc(digits[(value >> (uint32_t)shift) & 15u]);
    debug_putc('\n');
}

static uint64_t read_tsc(void) {
    uint32_t low, high;
    __asm__ volatile("rdtsc" : "=a"(low), "=d"(high));
    return ((uint64_t)high << 32) | low;
}

static void set_control(slice_controls *controls, const char *key,
                        UINTN key_size, const char *value, UINTN value_size) {
    CHAR16 mapped = value_size == 1u ? (CHAR16)(uint8_t)value[0] : 0u;
    if (value_size == 5u && value[0]=='s'&&value[1]=='p'&&value[2]=='a'&&
        value[3]=='c'&&value[4]=='e') mapped = ' ';
    if (mapped == 0u) return;
#define MATCH(text) (key_size==sizeof(text)-1u && bytes_equal(key,text,sizeof(text)-1u))
    if (MATCH("left")) controls->left = mapped;
    else if (MATCH("right")) controls->right = mapped;
    else if (MATCH("sound")) controls->sound = mapped;
    else if (MATCH("exit")) controls->exit = mapped;
#undef MATCH
}

static void load_controls(EFI_HANDLE image, EFI_BOOT_SERVICES *bs,
                          slice_controls *controls) {
    static const CHAR16 path[] = L"games\\integrated\\controls.cfg";
    EFI_LOADED_IMAGE_PROTOCOL *loaded = 0;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = 0;
    EFI_FILE_PROTOCOL *root = 0, *file = 0;
    char bytes[256]; UINTN size = sizeof(bytes); UINTN start = 0u, i;
    if (EFI_ERROR(bs->handle_protocol(image,&loaded_image_guid,(void**)&loaded)) ||
        EFI_ERROR(bs->handle_protocol(loaded->device_handle,&simple_fs_guid,(void**)&fs)) ||
        EFI_ERROR(fs->open_volume(fs,&root)) ||
        EFI_ERROR(root->open(root,&file,path,EFI_FILE_MODE_READ,0u)) ||
        EFI_ERROR(file->read(file,&size,bytes))) goto done;
    for (i=0u;i<=size;++i) if (i==size||bytes[i]=='\n'||bytes[i]=='\r') {
        UINTN eq=start;
        while(eq<i&&bytes[eq]!='=')++eq;
        if(eq<i)set_control(controls,bytes+start,eq-start,bytes+eq+1u,i-eq-1u);
        while(i+1u<size&&(bytes[i+1u]=='\n'||bytes[i+1u]=='\r'))++i;
        start=i+1u;
    }
done:
    if(file!=0)(void)file->close(file);
    if(root!=0)(void)root->close(root);
}

static int bytes_equal(const void *left, const void *right, UINTN size) {
    const uint8_t *a = left;
    const uint8_t *b = right;
    while (size-- != 0u) if (*a++ != *b++) return 0;
    return 1;
}

static void make_audio(void) {
    UINTN frame;
    uint32_t music_phase = 0u, effect_phase = 0u;
    for (frame = 0u; frame < MUSIC_FRAMES; ++frame) {
        int16_t value = (music_phase & 0x80000000u) != 0u ? 3000 : -3000;
        music_samples[frame * 2u] = value;
        music_samples[frame * 2u + 1u] = value;
        music_phase += (uint32_t)(((uint64_t)330u << 32) / OC_AUDIO_MIX_RATE);
    }
    for (frame = 0u; frame < EFFECT_FRAMES; ++frame) {
        effect_samples[frame] = (effect_phase & 0x80000000u) != 0u ? 7000 : -7000;
        effect_phase += (uint32_t)(((uint64_t)990u << 32) / OC_AUDIO_MIX_RATE);
    }
}

static EFI_STATUS submit_audio(OC_AUDIO_PROTOCOL *audio, OC_AUDIO_VOICE *voice,
                               const int16_t *samples, UINTN frames) {
    OC_AUDIO_BUFFER buffer = {sizeof(buffer), OC_AUDIO_SUBMIT_COPY, samples, frames, 0u};
    uint32_t result = OC_AUDIO_RESULT_BACKEND_FAILURE;
    EFI_STATUS status = audio->submit(voice, &buffer, &result);
    if (EFI_ERROR(status)) return status;
    return result == OC_AUDIO_RESULT_OK ? EFI_SUCCESS : EFI_NOT_READY;
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *system_table) {
    static const char request[] = "openconsole-integrated-probe";
    static const char expected[] = "openconsole-udp-reply";
    EFI_BOOT_SERVICES *bs = system_table->boot_services;
    OC_AUDIO_PROTOCOL *audio = 0;
    OC_NET_PROTOCOL *net = 0;
    OC_GLES2_PROTOCOL *gles = 0;
    OC_GLES_CONTEXT *gles_context = 0;
    OC_GLES_CONTEXT_DESC gles_desc = {sizeof(gles_desc),OC_GLES_ABI_VERSION,
        0u,0u,OC_GLES_CONTEXT_SOFTWARE,{0u,0u,0u}};
    PFNGLCLEARCOLORPROC clear_color; PFNGLCLEARPROC clear;
    PFNGLCREATESHADERPROC create_shader; PFNGLSHADERSOURCEPROC shader_source;
    PFNGLCOMPILESHADERPROC compile_shader; PFNGLGETSHADERIVPROC get_shader_iv;
    PFNGLCREATEPROGRAMPROC create_program; PFNGLATTACHSHADERPROC attach_shader;
    PFNGLLINKPROGRAMPROC link_program; PFNGLGETPROGRAMIVPROC get_program_iv;
    PFNGLUSEPROGRAMPROC use_program; PFNGLGETATTRIBLOCATIONPROC get_attrib;
    PFNGLVERTEXATTRIBPOINTERPROC attrib; PFNGLENABLEVERTEXATTRIBARRAYPROC enable;
    PFNGLDRAWARRAYSPROC draw;
    const GLchar *vs_source=OC_GLES_VERTEX_SHADER_TEMPLATE;
    const GLchar *fs_source=OC_GLES_FRAGMENT_SHADER_TEMPLATE;
    GLfloat positions[]={-0.60f,-0.55f,0.60f,-0.55f,0.0f,0.60f};
    static const GLfloat colors[]={1,0,0,0,1,0,0,0,1};
    GLuint vs,fs,program; GLint shader_ok,position_location,color_location;
    OC_AUDIO_VOICE *music = 0, *effect = 0;
    OC_AUDIO_VOICE_DESC music_desc = {sizeof(music_desc), OC_AUDIO_ABI_VERSION,
        OC_AUDIO_MIX_RATE, 2u, 16u, 65536, 0, 1024u * 1024u};
    OC_AUDIO_VOICE_DESC effect_desc = {sizeof(effect_desc), OC_AUDIO_ABI_VERSION,
        OC_AUDIO_MIX_RATE, 1u, 16u, 65536, 65536, 256u * 1024u};
    OC_NET_SESSION *session = 0;
    OC_NET_OPERATION *operation = 0;
    OC_NET_SESSION_DESC session_desc = {sizeof(session_desc), OC_NET_ABI_VERSION,
                                         8192u, 2u, 0u};
    OC_NET_DEADLINE deadline = {sizeof(deadline), 0u, 15000000u};
    OC_NET_OPERATION_STATUS operation_status = {sizeof(operation_status), 0u, 0u, 0u, 0u, 0u};
    OC_NET_TRANSFER_DESC transfer;
    uint8_t response[64];
    EFI_EVENT timer = 0;
    EFI_EVENT events[3];
    EFI_SIMPLE_POINTER_PROTOCOL *pointer = 0;
    EFI_HANDLE *pointer_handles = 0;
    UINTN pointer_count = 0u, pointer_index;
    UINTN event_count = 2u;
    EFI_INPUT_KEY key;
    uint64_t now_usec = 0u;
    uint64_t operation_started_usec = 0u;
    UINTN event_index = 0u;
    UINTN frame = 0u;
    int32_t horizontal_q16 = 0;
    slice_controls controls = {'j','l','k','q'};
    uint64_t render_cycles = 0u, max_render_cycles = 0u;
    uint32_t net_stage = 1u;
    EFI_STATUS status;
    load_controls(image_handle,bs,&controls);
    debug_write("OPENCONSOLE_SLICE_KEYMAP_LEFT_");debug_putc((char)controls.left);debug_putc('\n');
    debug_write("OPENCONSOLE_SLICE_KEYMAP_RIGHT_");debug_putc((char)controls.right);debug_putc('\n');
    debug_write("OPENCONSOLE_SLICE_KEYMAP_SOUND_");debug_putc((char)controls.sound);debug_putc('\n');
    debug_write("OPENCONSOLE_SLICE_KEYMAP_EXIT_");debug_putc((char)controls.exit);debug_putc('\n');

    if (EFI_ERROR(bs->locate_protocol(&OC_AUDIO_PROTOCOL_GUID, 0, (void **)&audio)) ||
        EFI_ERROR(bs->locate_protocol(&OC_NET_PROTOCOL_GUID, 0, (void **)&net)) ||
        EFI_ERROR(bs->locate_protocol(&OC_GLES2_PROTOCOL_GUID, 0, (void **)&gles)) ||
        audio == 0 || net == 0 || gles == 0 ||
        gles->abi_version != OC_GLES_ABI_VERSION) {
        debug_write("OPENCONSOLE_INTEGRATED_PROTOCOL_MISSING\n");
        return EFI_NOT_FOUND;
    }
    if (EFI_ERROR(gles->create_context(&gles_desc,&gles_context)) ||
        EFI_ERROR(gles->make_current(gles_context))) {
        debug_write("OPENCONSOLE_INTEGRATED_GLES_SETUP_FAILED\n");
        return EFI_UNSUPPORTED;
    }
    clear_color=RESOLVE(PFNGLCLEARCOLORPROC,"glClearColor");
    clear=RESOLVE(PFNGLCLEARPROC,"glClear");
    create_shader=RESOLVE(PFNGLCREATESHADERPROC,"glCreateShader");
    shader_source=RESOLVE(PFNGLSHADERSOURCEPROC,"glShaderSource");
    compile_shader=RESOLVE(PFNGLCOMPILESHADERPROC,"glCompileShader");
    get_shader_iv=RESOLVE(PFNGLGETSHADERIVPROC,"glGetShaderiv");
    create_program=RESOLVE(PFNGLCREATEPROGRAMPROC,"glCreateProgram");
    attach_shader=RESOLVE(PFNGLATTACHSHADERPROC,"glAttachShader");
    link_program=RESOLVE(PFNGLLINKPROGRAMPROC,"glLinkProgram");
    get_program_iv=RESOLVE(PFNGLGETPROGRAMIVPROC,"glGetProgramiv");
    use_program=RESOLVE(PFNGLUSEPROGRAMPROC,"glUseProgram");
    get_attrib=RESOLVE(PFNGLGETATTRIBLOCATIONPROC,"glGetAttribLocation");
    attrib=RESOLVE(PFNGLVERTEXATTRIBPOINTERPROC,"glVertexAttribPointer");
    enable=RESOLVE(PFNGLENABLEVERTEXATTRIBARRAYPROC,"glEnableVertexAttribArray");
    draw=RESOLVE(PFNGLDRAWARRAYSPROC,"glDrawArrays");
    if(clear_color==0||clear==0||create_shader==0||shader_source==0||
       compile_shader==0||get_shader_iv==0||create_program==0||attach_shader==0||
       link_program==0||get_program_iv==0||use_program==0||get_attrib==0||
       attrib==0||enable==0||draw==0) return EFI_UNSUPPORTED;
    vs=create_shader(GL_VERTEX_SHADER); fs=create_shader(GL_FRAGMENT_SHADER);
    shader_source(vs,1,&vs_source,0); shader_source(fs,1,&fs_source,0);
    compile_shader(vs); compile_shader(fs);
    get_shader_iv(vs,GL_COMPILE_STATUS,&shader_ok); if(!shader_ok)return EFI_UNSUPPORTED;
    get_shader_iv(fs,GL_COMPILE_STATUS,&shader_ok); if(!shader_ok)return EFI_UNSUPPORTED;
    program=create_program(); attach_shader(program,vs); attach_shader(program,fs);
    link_program(program); get_program_iv(program,GL_LINK_STATUS,&shader_ok);
    if(!shader_ok)return EFI_UNSUPPORTED; use_program(program);
    position_location=get_attrib(program,"a_position");
    color_location=get_attrib(program,"a_color");
    attrib((GLuint)color_location,3,GL_FLOAT,GL_FALSE,0,colors);
    enable((GLuint)position_location); enable((GLuint)color_location);
    make_audio();
    if (EFI_ERROR(audio->create_voice(&music_desc, &music)) ||
        EFI_ERROR(audio->create_voice(&effect_desc, &effect)) ||
        EFI_ERROR(submit_audio(audio, music, music_samples, MUSIC_FRAMES)) ||
        EFI_ERROR(net->create_session(&session_desc, &session)) ||
        EFI_ERROR(net->configure_dhcp4(session, &deadline, &operation)) ||
        EFI_ERROR(bs->create_event(EFI_EVENT_TIMER, EFI_TPL_CALLBACK, 0, 0, &timer)) ||
        EFI_ERROR(bs->set_timer(timer, EFI_TIMER_PERIODIC, 166667u))) {
        debug_write("OPENCONSOLE_INTEGRATED_SETUP_FAILED\n");
        return EFI_DEVICE_ERROR;
    }
    events[0] = system_table->con_in->wait_for_key;
    events[1] = timer;
    if (!EFI_ERROR(bs->locate_handle_buffer(EFI_LOCATE_BY_PROTOCOL,
            &simple_pointer_guid,0,&pointer_count,&pointer_handles))) {
        for(pointer_index=0u;pointer_index<pointer_count;++pointer_index) {
            EFI_SIMPLE_POINTER_PROTOCOL *candidate=0;
            if(!EFI_ERROR(bs->handle_protocol(pointer_handles[pointer_index],
                    &simple_pointer_guid,(void**)&candidate))&&candidate!=0&&
               candidate->reset!=0&&candidate->get_state!=0&&
               candidate->wait_for_input!=0&&candidate->mode!=0&&
               candidate->mode->left_button&&
               !EFI_ERROR(candidate->reset(candidate,0u))) pointer=candidate;
        }
        (void)bs->free_pool(pointer_handles);
    }
    if(pointer!=0){events[event_count++]=pointer->wait_for_input;
        debug_write("OPENCONSOLE_SLICE_POINTER_READY\n");}
    else debug_write("OPENCONSOLE_SLICE_POINTER_ABSENT\n");
    (void)audio->pump(0u);
    debug_write("OPENCONSOLE_INTEGRATED_CHILD_READY\n");

    for (;;) {
        status = bs->wait_for_event(event_count, events, &event_index);
        if (EFI_ERROR(status)) continue;
        /* Some USB HID implementations update state without reliably
         * signaling WaitForInput after an image handoff. Poll on the frame
         * timer as well so the child retains mouse input across StartImage. */
        if(pointer!=0){EFI_SIMPLE_POINTER_STATE mouse;
            while(!EFI_ERROR(pointer->get_state(pointer,&mouse))){
                horizontal_q16+=mouse.relative_movement_x*256;
                if(horizontal_q16<-19661)horizontal_q16=-19661;
                if(horizontal_q16>19661)horizontal_q16=19661;
                if(mouse.relative_movement_x!=0)debug_write("OPENCONSOLE_SLICE_MOUSE_MOVED\n");
                if(mouse.left_button&&!EFI_ERROR(submit_audio(audio,effect,effect_samples,EFFECT_FRAMES)))
                    debug_write("OPENCONSOLE_SLICE_MOUSE_SOUND\n");
            }
            if(event_index==2u)continue;
        }
        if (event_index == 0u && !EFI_ERROR(system_table->con_in->read_key_stroke(
                system_table->con_in, &key))) {
            if (key.unicode_char == controls.sound) {
                if (!EFI_ERROR(submit_audio(audio, effect, effect_samples, EFFECT_FRAMES)))
                    debug_write("OPENCONSOLE_INTEGRATED_EFFECT_TRIGGERED\n");
            } else if (key.unicode_char == controls.left) {
                horizontal_q16-=4096;if(horizontal_q16<-19661)horizontal_q16=-19661;
                debug_write("OPENCONSOLE_SLICE_KEY_LEFT\n");
            } else if (key.unicode_char == controls.right) {
                horizontal_q16+=4096;if(horizontal_q16>19661)horizontal_q16=19661;
                debug_write("OPENCONSOLE_SLICE_KEY_RIGHT\n");
            } else if (key.unicode_char == controls.exit) {
                OC_AUDIO_VOICE_STATUS voice_status={sizeof(voice_status),0u,0u,0u,0u,0u,0u,0u};
                debug_write("OPENCONSOLE_INTEGRATED_CHILD_RETURNING\n");
                if (!EFI_ERROR(audio->query_voice(music,&voice_status))) {
                    debug_write("OPENCONSOLE_INTEGRATED_AUDIO_UNDERRUNS_");
                    debug_hex64(voice_status.underrun_count);
                    debug_write("OPENCONSOLE_INTEGRATED_AUDIO_LATENCY_FRAMES_");
                    debug_hex64(voice_status.output_latency_frames);
                }
                debug_write("OPENCONSOLE_INTEGRATED_FRAME_COUNT_"); debug_hex64(frame);
                debug_write("OPENCONSOLE_SLICE_RENDER_CYCLES_TOTAL_");debug_hex64(render_cycles);
                debug_write("OPENCONSOLE_SLICE_RENDER_CYCLES_MAX_");debug_hex64(max_render_cycles);
                (void)bs->close_event(timer);
                (void)gles->make_current(0);
                (void)gles->destroy_context(gles_context);
                return EFI_SUCCESS;
            } else if (key.unicode_char == 'e' || key.unicode_char == 'E') {
                OC_AUDIO_VOICE_STATUS voice_status={sizeof(voice_status),0u,0u,0u,0u,0u,0u,0u};
                debug_write("OPENCONSOLE_INTEGRATED_CHILD_ERROR_RETURNING\n");
                if (!EFI_ERROR(audio->query_voice(music,&voice_status))) {
                    debug_write("OPENCONSOLE_INTEGRATED_AUDIO_UNDERRUNS_");
                    debug_hex64(voice_status.underrun_count);
                    debug_write("OPENCONSOLE_INTEGRATED_AUDIO_LATENCY_FRAMES_");
                    debug_hex64(voice_status.output_latency_frames);
                }
                debug_write("OPENCONSOLE_INTEGRATED_FRAME_COUNT_"); debug_hex64(frame);
                (void)bs->close_event(timer);
                (void)gles->make_current(0);
                (void)gles->destroy_context(gles_context);
                return EFI_DEVICE_ERROR;
            }
            continue;
        }
        now_usec += 16667u;
        (void)audio->pump(now_usec);
        (void)net->pump(session, now_usec);
        {uint64_t cycle_start=read_tsc();
        positions[0]=(GLfloat)horizontal_q16/65536.0f-0.60f;
        positions[2]=positions[0]+1.20f; positions[4]=positions[0]+0.60f;
        clear_color((GLfloat)(frame&31u)/128.0f,0.08f,0.12f,1.0f);
        clear(GL_COLOR_BUFFER_BIT);
        attrib((GLuint)position_location,2,GL_FLOAT,GL_FALSE,0,positions);
        draw(GL_TRIANGLES,0,3);
        if (EFI_ERROR(gles->present(gles_context))) {
            debug_write("OPENCONSOLE_INTEGRATED_GLES_PRESENT_FAILED\n");
            return EFI_DEVICE_ERROR;
        }
        {uint64_t cycles=read_tsc()-cycle_start;render_cycles+=cycles;
         if(cycles>max_render_cycles)max_render_cycles=cycles;}}
        ++frame;
        if ((frame & 15u) == 0u) debug_write("OPENCONSOLE_INTEGRATED_FRAME\n");

        if (operation != 0) {
            operation_status.structure_size = sizeof(operation_status);
            if (!EFI_ERROR(net->query_operation(operation, &operation_status)) &&
                operation_status.result != OC_NET_RESULT_PENDING) {
                if (operation_status.result != OC_NET_RESULT_OK) {
                    debug_write("OPENCONSOLE_INTEGRATED_NETWORK_FAILED\n");
                    net_stage = 0u;
                }
                (void)net->release_operation(operation);
                operation = 0;
                if (net_stage == 1u && operation_status.result == OC_NET_RESULT_OK) {
                    debug_write("OPENCONSOLE_INTEGRATED_DHCP_USEC_");
                    debug_hex64(now_usec-operation_started_usec);
                    transfer = (OC_NET_TRANSFER_DESC){sizeof(transfer), FIXTURE_ADDRESS,
                        9000u, 0u, request, sizeof(request) - 1u,
                        response, sizeof(response),
                        {sizeof(OC_NET_DEADLINE), 0u, now_usec + 5000000u}};
                    if (!EFI_ERROR(net->start_udp4(session, &transfer, &operation)))
                        { net_stage = 2u; operation_started_usec=now_usec; }
                } else if (net_stage == 2u &&
                           operation_status.bytes_transferred == sizeof(expected) - 1u &&
                           bytes_equal(response, expected, sizeof(expected) - 1u)) {
                    net_stage = 3u;
                    debug_write("OPENCONSOLE_INTEGRATED_NETWORK_OK\n");
                    debug_write("OPENCONSOLE_INTEGRATED_UDP_USEC_");
                    debug_hex64(now_usec-operation_started_usec);
                }
            }
        }
    }
}
