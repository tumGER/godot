#include <switch.h>
#include "os_switch.h"
#include "context_gl_switch_egl.h"

#include "drivers/unix/file_access_unix.h"
#include "drivers/unix/dir_access_unix.h"
#include "drivers/unix/ip_unix.h"
#include "drivers/unix/tcp_server_posix.h"
#include "drivers/unix/stream_peer_tcp_posix.h"
#include "drivers/unix/packet_peer_udp_posix.h"

#include "drivers/switch/thread_switch.h"
#include "drivers/switch/mutex_switch.h"
#include "drivers/switch/semaphore_switch.h"
#include "drivers/switch/rwlock_switch.h"
#include "servers/audio_server.h"
#include "servers/visual/visual_server_wrap_mt.h"
#include "drivers/gles3/rasterizer_gles3.h"
#include "drivers/gles2/rasterizer_gles2.h"
#include "main/main.h"
#include <stdio.h>


#define ENABLE_NXLINK

#ifndef ENABLE_NXLINK
#define TRACE(fmt,...) ((void)0)
#else
#include <unistd.h>
#define TRACE(fmt,...) printf("%s: " fmt "\n", __PRETTY_FUNCTION__, ## __VA_ARGS__)
#endif

void OS_Switch::initialize_core()
{
	ThreadSwitch::make_default();
	SemaphoreSwitch::make_default();
	MutexSwitch::make_default();
	RWLockSwitch::make_default();

	FileAccess::make_default<FileAccessUnix>(FileAccess::ACCESS_RESOURCES);
	FileAccess::make_default<FileAccessUnix>(FileAccess::ACCESS_USERDATA);
	FileAccess::make_default<FileAccessUnix>(FileAccess::ACCESS_FILESYSTEM);
	//FileAccessBufferedFA<FileAccessUnix>::make_default();
	DirAccess::make_default<DirAccessUnix>(DirAccess::ACCESS_RESOURCES);
	DirAccess::make_default<DirAccessUnix>(DirAccess::ACCESS_USERDATA);
	DirAccess::make_default<DirAccessUnix>(DirAccess::ACCESS_FILESYSTEM);

#ifndef NO_NETWORK
	TCPServerPosix::make_default();
	StreamPeerTCPPosix::make_default();
	PacketPeerUDPPosix::make_default();
	IP_Unix::make_default();
#endif
}

void OS_Switch::swap_buffers()
{
#if defined(OPENGL_ENABLED)
	gl_context->swap_buffers();
#endif
}

Error OS_Switch::initialize(const VideoMode &p_desired, int p_video_driver, int p_audio_driver)
{
#if defined(OPENGL_ENABLED)
	bool gles3_context = true;
	if (p_video_driver == VIDEO_DRIVER_GLES2) {
		gles3_context = false;
	}

	bool editor = Engine::get_singleton()->is_editor_hint();
	bool gl_initialization_error = false;

	gl_context = NULL;
	while (!gl_context) {
		gl_context = memnew(ContextGLSwitchEGL(gles3_context));

		if (gl_context->initialize() != OK) {
			memdelete(gl_context);
			gl_context = NULL;

			if (GLOBAL_GET("rendering/quality/driver/driver_fallback") == "Best" || editor) {
				if (p_video_driver == VIDEO_DRIVER_GLES2) {
					gl_initialization_error = true;
					break;
				}

				p_video_driver = VIDEO_DRIVER_GLES2;
				gles3_context = false;
			} else {
				gl_initialization_error = true;
				break;
			}
		}
	}

	while (true) {
		if (gles3_context) {
			if (RasterizerGLES3::is_viable() == OK) {
				RasterizerGLES3::register_config();
				RasterizerGLES3::make_current();
				break;
			} else {
				if (GLOBAL_GET("rendering/quality/driver/driver_fallback") == "Best" || editor) {
					p_video_driver = VIDEO_DRIVER_GLES2;
					gles3_context = false;
					continue;
				} else {
					gl_initialization_error = true;
					break;
				}
			}
		} else {
			if (RasterizerGLES2::is_viable() == OK) {
				RasterizerGLES2::register_config();
				RasterizerGLES2::make_current();
				break;
			} else {
				gl_initialization_error = true;
				break;
			}
		}
	}

	if (gl_initialization_error) {
		OS::get_singleton()->alert("Your video card driver does not support any of the supported OpenGL versions.\n"
								   "Please update your drivers or if you have a very old or integrated GPU upgrade it.",
				"Unable to initialize Video driver");
		return ERR_UNAVAILABLE;
	}

	video_driver_index = p_video_driver;

	gl_context->set_use_vsync(current_videomode.use_vsync);
#endif

	visual_server = memnew(VisualServerRaster);
	if (get_render_thread_mode() != RENDER_THREAD_UNSAFE)
	{
		visual_server = memnew(VisualServerWrapMT(visual_server, get_render_thread_mode() == RENDER_SEPARATE_THREAD));
	}

	visual_server->init();

	input = memnew(InputDefault);
	//joypad = memnew(JoypadSwitch(input));

	power_manager = memnew(PowerSwitch);

	AudioDriverManager::initialize(p_audio_driver);

	//_ensure_user_data_dir();

	return OK;
}

void OS_Switch::set_main_loop(MainLoop *p_main_loop)
{
	input->set_main_loop(p_main_loop);
	main_loop = p_main_loop;
}

void OS_Switch::delete_main_loop()
{
	if (main_loop)
		memdelete(main_loop);
	main_loop = NULL;
}

void OS_Switch::finalize()
{
	delete_main_loop();

	memdelete(input);
	visual_server->finish();
	memdelete(visual_server);
	memdelete(power_manager);
	memdelete(gl_context);
}

void OS_Switch::finalize_core()
{
}

bool OS_Switch::_check_internal_feature_support(const String &p_feature) { return false; }

void OS_Switch::alert(const String &p_alert, const String &p_title)
{
	printf("got alert %s", p_alert.c_str());
}
String OS_Switch::get_stdin_string(bool p_block) { return ""; }
Point2 OS_Switch::get_mouse_position() const
{
	return Point2(0, 0);
}

int OS_Switch::get_mouse_button_state() const
{
	return 0;
}

void OS_Switch::set_window_title(const String &p_title) {}

void OS_Switch::set_video_mode(const OS::VideoMode &p_video_mode, int p_screen) {}
OS::VideoMode OS_Switch::get_video_mode(int p_screen) const
{
	return VideoMode(1280, 720);
}

void OS_Switch::get_fullscreen_mode_list(List<OS::VideoMode> *p_list, int p_screen) const {}

int OS_Switch::get_current_video_driver() const { return video_driver_index; }
Size2 OS_Switch::get_window_size() const { return Size2(1280, 720); }

Error OS_Switch::execute(const String &p_path, const List<String> &p_arguments, bool p_blocking, ProcessID *r_child_id, String *r_pipe, int *r_exitcode, bool read_stderr)
{
	return FAILED;
}

Error OS_Switch::kill(const ProcessID &p_pid)
{
	return FAILED;
}

bool OS_Switch::has_environment(const String &p_var) const
{
	return false;
}

String OS_Switch::get_environment(const String &p_var) const
{
	return "";
}

String OS_Switch::get_name()
{
	return "Switch";
}

MainLoop * OS_Switch::get_main_loop() const
{
	return main_loop;
}

OS::Date OS_Switch::get_date(bool local) const
{
	return OS::Date();
}

OS::Time OS_Switch::get_time(bool local) const
{
	return OS::Time();
}

OS::TimeZoneInfo OS_Switch::get_time_zone_info() const
{
	return OS::TimeZoneInfo();
}

void OS_Switch::delay_usec(uint32_t p_usec) const
{
	svcSleepThread(p_usec);
}

uint64_t OS_Switch::get_ticks_usec() const
{
	static u64 tick_freq = armGetSystemTickFreq();
	return armGetSystemTick() / tick_freq;
}

bool OS_Switch::can_draw() const
{
	return true;
}

void OS_Switch::set_cursor_shape(CursorShape p_shape) {}
void OS_Switch::set_custom_mouse_cursor(const RES &p_cursor, CursorShape p_shape, const Vector2 &p_hotspot) {}

void OS_Switch::run()
{
	bool force_quit = false;

	if (!main_loop)
	{
		TRACE("no main loop???\n");
		return;
	}

	main_loop->init();
	while(appletMainLoop())
	{
		if (Main::iteration() == true)
			break;
	}
	main_loop->finish();
}


OS::PowerState OS_Switch::get_power_state() {
	return power_manager->get_power_state();
}

int OS_Switch::get_power_seconds_left() {
	return power_manager->get_power_seconds_left();
}

int OS_Switch::get_power_percent_left() {
	return power_manager->get_power_percent_left();
}

OS_Switch::OS_Switch()
{
	video_driver_index = 0;
	main_loop = nullptr;
	visual_server = nullptr;
	input = nullptr;
	power_manager = nullptr;
	gl_context = nullptr;
}