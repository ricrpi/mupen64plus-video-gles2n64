
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <assert.h>

#ifdef VC
#include <bcm_host.h>
#endif

#include "Common.h"
#include "gles2N64.h"
#include "OpenGL.h"
#include "Types.h"
#include "N64.h"
#include "gSP.h"
#include "gDP.h"
#include "Textures.h"
#include "ShaderCombiner.h"
#include "VI.h"
#include "RSP.h"
#include "Config.h"
#include "ticks.h"
#include "OGLDebug.h"
#include "FrameSkipper.h"


//#define DEBUG_PRINT(...) printf(__VA_ARGS__)

#ifndef DEBUG_PRINT
#define DEBUG_PRINT(...)
#endif

//// paulscode, function prototype missing from Yongzh's code
void OGL_UpdateDepthUpdate();
////

#ifdef TEXTURECACHE_TEST
int     TextureCacheTime = 0;
#endif


#ifdef RENDERSTATE_TEST
int     StateChanges = 0;
#endif

#ifdef SHADER_TEST
int     ProgramSwaps = 0;
#endif

#ifdef BATCH_TEST
int     TotalDrawTime = 0;
int     TotalTriangles = 0;
int     TotalDrawCalls = 0;

#define glDrawElements(A,B,C,D) \
    TotalTriangles += B; TotalDrawCalls++; int t = ticksGetTicks(); glDrawElements(A,B,C,D); OPENGL_CHECK_ERRORS; TotalDrawTime += (ticksGetTicks() - t);
#define glDrawArrays(A,B,C) \
    TotalTriangles += C; TotalDrawCalls++; int t = ticksGetTicks(); glDrawArrays(A,B,C); OPENGL_CHECK_ERRORS; TotalDrawTime += (ticksGetTicks() - t);

#endif

#ifdef VC
static unsigned g_fb_width;
static unsigned g_fb_height;
#endif

GLInfo OGL;

const char _default_vsh[] = "                           \n\t" \
"attribute highp vec2 aPosition;                        \n\t" \
"attribute highp vec2 aTexCoord;                        \n\t" \
"varying mediump vec2 vTexCoord;                        \n\t" \
"void main(){                                           \n\t" \
"gl_Position = vec4(aPosition.x, aPosition.y, 0.0, 1.0);\n\t" \
"vTexCoord = aTexCoord;                                 \n\t" \
"}                                                      \n\t";

const char _default_fsh[] = "                           \n\t" \
"uniform sampler2D uTex;                                \n\t" \
"varying mediump vec2 vTexCoord;                        \n\t" \
"void main(){                                           \n\t" \
"gl_FragColor = texture2D(uTex, vTexCoord);             \n\t" \
"}                                                      \n\t";

void OGL_EnableRunfast()
{
#ifdef ARM_ASM
	static const unsigned int x = 0x04086060;
	static const unsigned int y = 0x03000000;
	int r;
	asm volatile (
		"fmrx	%0, fpscr			\n\t"	//r0 = FPSCR
		"and	%0, %0, %1			\n\t"	//r0 = r0 & 0x04086060
		"orr	%0, %0, %2			\n\t"	//r0 = r0 | 0x03000000
		"fmxr	fpscr, %0			\n\t"	//FPSCR = r0
		: "=r"(r)
		: "r"(x), "r"(y)
	);
#endif
}

int OGL_IsExtSupported( const char *extension )
{
	const GLubyte *extensions = NULL;
	const GLubyte *start;
	GLubyte *where, *terminator;

	where = (GLubyte *) strchr(extension, ' ');
	if (where || *extension == '\0')
		return 0;

	extensions = glGetString(GL_EXTENSIONS);
	OPENGL_CHECK_ERRORS;

    if (!extensions) return 0;

	start = extensions;
	for (;;)
	{
		where = (GLubyte *) strstr((const char *) start, extension);
		if (!where)
			break;

		terminator = where + strlen(extension);
		if (where == start || *(where - 1) == ' ')
			if (*terminator == ' ' || *terminator == '\0')
				return 1;

		start = terminator;
	}

	return 0;
}

extern void _glcompiler_error(GLint shader);

void OGL_InitStates()
{
    GLint   success;

    glEnable( GL_CULL_FACE );
	OPENGL_CHECK_ERRORS;

    glEnableVertexAttribArray( SC_POSITION );
	OPENGL_CHECK_ERRORS;

    glEnable( GL_DEPTH_TEST );
	OPENGL_CHECK_ERRORS;

    glDepthFunc( GL_ALWAYS );
	OPENGL_CHECK_ERRORS;

    glDepthMask( GL_FALSE );
	OPENGL_CHECK_ERRORS;

    glEnable( GL_SCISSOR_TEST );
	OPENGL_CHECK_ERRORS;

///// paulscode, fixes missing graphics on Qualcomm, Adreno:
    glDepthRangef(0.0f, 1.0f);
	OPENGL_CHECK_ERRORS;
/*
    // default values (only seem to work on OMAP!)
    glPolygonOffset(0.2f, 0.2f);
*/
    //// paulscode, added for different configurations based on hardware
    // (part of the missing shadows and stars bug fix)
    //int hardwareType = Android_JNI_GetHardwareType();
    //float f1, f2;
    //Android_JNI_GetPolygonOffset(hardwareType, 1, &f1, &f2);
    //glPolygonOffset( f1, f2 );
    ////

// some other settings that have been tried, which do not work:
    //glDepthRangef(1.0f, 0.0f);  // reverses depth-order on OMAP3 chipsets
    //glPolygonOffset(-0.2f, -0.2f);
    //glDepthRangef( 0.09f, (float)0x7FFF );  // should work, but not on Adreno
    //glPolygonOffset( -0.2f, 0.2f );
    //glDepthRangef(0.0f, (float)0x7FFF);  // what Yongzh used, broken on Adreno
    //glPolygonOffset(0.2f, 0.2f);
/////
	
	glPolygonOffset(-3.0f, -3.0f);
    
	DEBUG_PRINT("Video: OpenGL.cpp:%d glViewport(%d,%d,%d,%d)\n", __LINE__, config.framebuffer.xpos, config.framebuffer.ypos, config.framebuffer.width, config.framebuffer.height);
    glViewport(config.framebuffer.xpos, config.framebuffer.ypos, config.framebuffer.width, config.framebuffer.height);
	OPENGL_CHECK_ERRORS;
    //create default shader program
    LOG( LOG_VERBOSE, "Generate Default Shader Program.\n" );

    const char *src[1];
    src[0] = _default_fsh;
    OGL.defaultFragShader = glCreateShader( GL_FRAGMENT_SHADER );
    glShaderSource( OGL.defaultFragShader, 1, (const char**) src, NULL );
	OPENGL_CHECK_ERRORS;

    glCompileShader( OGL.defaultFragShader );
	OPENGL_CHECK_ERRORS;

    glGetShaderiv( OGL.defaultFragShader, GL_COMPILE_STATUS, &success );
	OPENGL_CHECK_ERRORS;
    if (!success)
    {
        LOG(LOG_ERROR, "Failed to produce default fragment shader.\n");
    }

    src[0] = _default_vsh;
    OGL.defaultVertShader = glCreateShader( GL_VERTEX_SHADER );
	OPENGL_CHECK_ERRORS;

    glShaderSource( OGL.defaultVertShader, 1, (const char**) src, NULL );
        OPENGL_CHECK_ERRORS;

    glCompileShader( OGL.defaultVertShader );
	OPENGL_CHECK_ERRORS;

    glGetShaderiv( OGL.defaultVertShader, GL_COMPILE_STATUS, &success );
	OPENGL_CHECK_ERRORS;
    if( !success )
    {
        LOG( LOG_ERROR, "Failed to produce default vertex shader.\n" );
        _glcompiler_error( OGL.defaultVertShader );
    }

    OGL.defaultProgram = glCreateProgram();
	OPENGL_CHECK_ERRORS;

    glBindAttribLocation( OGL.defaultProgram, 0, "aPosition" );
	OPENGL_CHECK_ERRORS;

    glBindAttribLocation( OGL.defaultProgram, 1, "aTexCoord" );
	OPENGL_CHECK_ERRORS;

    glAttachShader( OGL.defaultProgram, OGL.defaultFragShader );
	OPENGL_CHECK_ERRORS;

    glAttachShader( OGL.defaultProgram, OGL.defaultVertShader );
	OPENGL_CHECK_ERRORS;

    glLinkProgram( OGL.defaultProgram );
	OPENGL_CHECK_ERRORS;
    glGetProgramiv( OGL.defaultProgram, GL_LINK_STATUS, &success );
	OPENGL_CHECK_ERRORS;
    if( !success )
    {
        LOG( LOG_ERROR, "Failed to link default program.\n" );
        _glcompiler_error( OGL.defaultFragShader );
    }

    glUseProgram( OGL.defaultProgram );
	OPENGL_CHECK_ERRORS;

    glUniform1i( glGetUniformLocation( OGL.defaultProgram, "uTex" ), 0 );
	OPENGL_CHECK_ERRORS;
}

void OGL_UpdateScale()
{
    if (VI.width == 0 || VI.height == 0)
        return;
    OGL.scaleX = (float)config.framebuffer.width / (float)VI.width;
    OGL.scaleY = (float)config.framebuffer.height / (float)VI.height;
}

void OGL_ResizeWindow(int x, int y, int width, int height)
{
    config.window.xpos = x;
    config.window.ypos = y;
    config.window.width = width;
    config.window.height = height;

    config.framebuffer.xpos = x;
    config.framebuffer.ypos = y;
    config.framebuffer.width = width;
    config.framebuffer.height = height;
    OGL_UpdateScale();

	DEBUG_PRINT("Video: OpenGL.cpp:%d glViewport(%d,%d,%d,%d)\n", __LINE__, config.framebuffer.xpos, config.framebuffer.ypos, config.framebuffer.width, config.framebuffer.height);
    glViewport(config.framebuffer.xpos, config.framebuffer.ypos,
            config.framebuffer.width, config.framebuffer.height);
	OPENGL_CHECK_ERRORS;
}

////// paulscode, added for SDL linkage
#ifdef USE_SDL
bool OGL_SDL_Start()
{
    /* Initialize SDL */
    LOG(LOG_MINIMAL, "Initializing SDL video subsystem...\n" );
    if (SDL_InitSubSystem( SDL_INIT_VIDEO ) == -1)
    {
         LOG(LOG_ERROR, "Error initializing SDL video subsystem: %s\n", SDL_GetError() );
        return FALSE;
    }

    int current_w = config.window.width;
    int current_h = config.window.height;

    /* Set the video mode */
    LOG(LOG_MINIMAL, "Setting video mode %dx%d...\n", current_w, current_h );

// TODO: I should actually check what the pixelformat is, rather than assuming 16 bpp (RGB_565) or 32 bpp (RGBA_8888):
//// paulscode, added for switching between modes RGBA8888 and RGB565
// (part of the color banding fix)
int bitsPP;
//if( Android_JNI_UseRGBA8888() )
    bitsPP = 32;
//else
//    bitsPP = 16;
/////

    // TODO: Replace SDL_SetVideoMode with something that is SDL 2.0 compatible
    //       Better yet, eliminate all SDL calls by using the Mupen64Plus core api
    if (!(OGL.hScreen = SDL_SetVideoMode( current_w, current_h, bitsPP, SDL_HWSURFACE )))
    {
        LOG(LOG_ERROR, "Problem setting videomode %dx%d: %s\n", current_w, current_h, SDL_GetError() );
        SDL_QuitSubSystem( SDL_INIT_VIDEO );
        return FALSE;
    }

//// paulscode, fixes the screen-size problem
    int videoWidth = current_w;
    int videoHeight = current_h;
    int x = 0;
    int y = 0;
    
    //re-scale width and height on per-rom basis
    float width = (float)videoWidth * (float)config.window.refwidth / 800.f;
    float height = (float)videoHeight * (float)config.window.refheight / 480.f;
    
    //re-center video if it was re-scaled per-rom
    x -= (width - (float)videoWidth) / 2.f;
    y -= (height - (float)videoHeight) / 2.f;
    
    //set xpos and ypos
    config.window.xpos = x;
    config.window.ypos = y;
    config.framebuffer.xpos = x;
    config.framebuffer.ypos = y;
    
    //set width and height
    config.window.width = (int)width;
    config.window.height = (int)height;
    config.framebuffer.width = (int)width;
    config.framebuffer.height = (int)height;
////
    return true;
}
#endif
//////

bool OGL_CoreVideo_Start()
{
    char m_strDeviceStats[200];
    m64p_video_flags flags = M64VIDEOFLAG_SUPPORT_RESIZING;

    LOG(LOG_MINIMAL, "Initializing core video subsystem...\n" );
    CoreVideo_Init();
    
    int current_w = config.window.width;
    int current_h = config.window.height;
	
#ifdef VC
	// Use VC get_display_size function to get the current screen resolution
	
    if (graphics_get_display_size(0 /* LCD */, &g_fb_width, &g_fb_height) < 0)
        printf("ERROR: Failed to get display size\n");
    if (config.useScreenResolution == 1)
    {
	config.window.width = g_fb_width;
	config.window.height = g_fb_height;
	config.framebuffer.width = g_fb_width;
	config.framebuffer.height = g_fb_height;
	current_w = g_fb_width;
	current_h = g_fb_height;
    }		
#endif
	
	/* Set the video mode */
    LOG(LOG_MINIMAL, "Setting video mode %dx%d...\n", current_w, current_h );
    if (CoreVideo_SetVideoMode(current_w, current_h, 32, M64VIDEO_FULLSCREEN, flags) != M64ERR_SUCCESS)
    {
	printf("ERROR: Failed to set %i-bit video mode: %ix%i\n", 32, config.window.width, config.window.height);
	return false;
    }

    /* hard-coded attribute values */
    const int iDOUBLEBUFFER = 1;

    /* set opengl attributes */
    CoreVideo_GL_SetAttribute(M64P_GL_DOUBLEBUFFER, iDOUBLEBUFFER);
    CoreVideo_GL_SetAttribute(M64P_GL_SWAP_CONTROL, config.verticalSync);
    
    /* use 16Bit RGB 565 color depth */
    CoreVideo_GL_SetAttribute(M64P_GL_BUFFER_SIZE, 16);
    CoreVideo_GL_SetAttribute(M64P_GL_RED_SIZE, 5);
    CoreVideo_GL_SetAttribute(M64P_GL_GREEN_SIZE, 6);
    CoreVideo_GL_SetAttribute(M64P_GL_BLUE_SIZE, 5);
    
    /* enable multisampling antialisasing */
    if (config.multiSampling > 0)
    {
        CoreVideo_GL_SetAttribute(M64P_GL_MULTISAMPLEBUFFERS, 1);
        if (config.multiSampling <= 2)
            CoreVideo_GL_SetAttribute(M64P_GL_MULTISAMPLESAMPLES, 2);
        else if (config.multiSampling <= 4)
            CoreVideo_GL_SetAttribute(M64P_GL_MULTISAMPLESAMPLES, 4);
        else if (config.multiSampling <= 8)
            CoreVideo_GL_SetAttribute(M64P_GL_MULTISAMPLESAMPLES, 8);
        else
            CoreVideo_GL_SetAttribute(M64P_GL_MULTISAMPLESAMPLES, 16);
    }
	
    const unsigned char* m_pRenderStr = glGetString(GL_RENDERER);
    const unsigned char* m_pVersionStr = glGetString(GL_VERSION);
    const unsigned char* m_pVendorStr = glGetString(GL_VENDOR);
    
    sprintf(m_strDeviceStats, "%.60s - %.128s : %.60s", m_pVendorStr, m_pRenderStr, m_pVersionStr);
    printf("Video: Using OpenGL: %s\n", m_strDeviceStats); //TODO should use core DebugMessage();
	
    //// paulscode, fixes the screen-size problem
    const float dstRatio = (float)current_h / (float)current_w;
    const float srcRatio = ( config.romPAL ? 9.0f/11.0f : 0.75f );
    int videoWidth = current_w;
    int videoHeight = current_h;
    int x = 0;
    int y = 0;
    
    //re-scale width and height on per-rom basis
    float width = current_w;
    float height = current_h;
    if (!config.stretchVideo) 
    {
   	// Dirty fix to keep aspect
   	// if source and destination aspect ratios are not equal recalculate videoWith/Height
   	if(dstRatio != srcRatio) 
   	{
   		videoWidth = height / srcRatio;
   		videoHeight = height;
   		if (videoWidth > width)
    		{
    		    videoWidth = width;
    		    videoHeight = width * srcRatio;
    		}
        }
        // else keep videoWidth/Height 
    }
    // else keep videoWidth/Height 

    x = (width - videoWidth) / 2;
    y = (height - videoHeight) / 2;
    
    //set xpos and ypos
    config.window.xpos = x;
    config.window.ypos = y;
    
    //set width and height
    config.window.width = (int)videoWidth;
    config.window.height = (int)videoHeight;
    //if (config.framebuffer.enable!=1) 
    //{
	config.framebuffer.xpos = x;
	config.framebuffer.ypos = y;
	config.framebuffer.width = (int)videoWidth;
	config.framebuffer.height = (int)videoHeight;
    //}

    return true;
}

bool OGL_Start()
{
// paulscode, initialize SDL
#ifdef USE_SDL
    if (!OGL_SDL_Start())
        return false;
#else
    if (!OGL_CoreVideo_Start())
        return false;
#endif
    OGL_InitStates();

#ifdef USE_SDL
/////// paulscode, graphics bug-fixes
    float depth = gDP.fillColor.z ;
    glDisable( GL_SCISSOR_TEST );
	OPENGL_CHECK_ERRORS;

    glDepthMask( GL_TRUE );  // fixes side-bar graphics glitches
	OPENGL_CHECK_ERRORS;

//    glClearDepthf( depth );  // broken on Qualcomm Adreno

    glClearDepthf( 1.0f );  // fixes missing graphics on Qualcomm Adreno
	OPENGL_CHECK_ERRORS;

    glClearColor( 0, 0, 0, 1 );
	OPENGL_CHECK_ERRORS;

    glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
	OPENGL_CHECK_ERRORS;

    glFinish();
	OPENGL_CHECK_ERRORS;

    Android_JNI_SwapWindow();  // paulscode, fix for black-screen bug
    glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
	OPENGL_CHECK_ERRORS;

    glFinish();
	OPENGL_CHECK_ERRORS;

    OGL_UpdateDepthUpdate();
    glEnable( GL_SCISSOR_TEST );
	OPENGL_CHECK_ERRORS;

////////
#endif

    //create framebuffer
    if (config.framebuffer.enable)
    {
        LOG(LOG_VERBOSE, "Create offscreen framebuffer. \n");
        if (config.framebuffer.width == config.window.width && config.framebuffer.height == config.window.height)
        {
            LOG(LOG_WARNING, "There's no point in using a offscreen framebuffer when the window and screen dimensions are the same\n");
        }

        glGenFramebuffers(1, &OGL.framebuffer.fb);
		OPENGL_CHECK_ERRORS;

        glGenRenderbuffers(1, &OGL.framebuffer.depth_buffer);
		OPENGL_CHECK_ERRORS;

        glGenTextures(1, &OGL.framebuffer.color_buffer);
		OPENGL_CHECK_ERRORS;

        glBindRenderbuffer(GL_RENDERBUFFER, OGL.framebuffer.depth_buffer);
		OPENGL_CHECK_ERRORS;

        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24_OES, config.framebuffer.width, config.framebuffer.height);
		OPENGL_CHECK_ERRORS;

        glBindTexture(GL_TEXTURE_2D, OGL.framebuffer.color_buffer);
		OPENGL_CHECK_ERRORS;

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, config.framebuffer.width, config.framebuffer.height, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, NULL);
		OPENGL_CHECK_ERRORS;

        glBindFramebuffer(GL_FRAMEBUFFER, OGL.framebuffer.fb);
		OPENGL_CHECK_ERRORS;

        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, OGL.framebuffer.color_buffer, 0);
		OPENGL_CHECK_ERRORS;

        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, OGL.framebuffer.depth_buffer);
		OPENGL_CHECK_ERRORS;

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		OPENGL_CHECK_ERRORS;

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
            LOG(LOG_ERROR, "Incomplete Framebuffer Object: ");
            switch(glCheckFramebufferStatus(GL_FRAMEBUFFER))
            {
                case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
                    printf("Incomplete Attachment. \n"); break;
                case GL_FRAMEBUFFER_UNSUPPORTED:
                    printf("Framebuffer Unsupported. \n"); break;
                case GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS:
                    printf("Incomplete Dimensions. \n"); break;
            }
            config.framebuffer.enable = 0;
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }
    }

    //check extensions
    GLfloat f = 0.0f;

    if (!OGL_IsExtSupported("GL_EXT_texture_filter_anistropic"))
    {
        LOG(LOG_WARNING, "Anistropic Filtering is not supported.\n");
        config.texture.maxAnisotropy = 0;
    }
    else
    {
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &f);
		OPENGL_CHECK_ERRORS;
    }

	if (config.texture.maxAnisotropy > ((int)f))
    {
        LOG(LOG_WARNING, "Clamping max anistropy to %ix.\n", (int)f);
        config.texture.maxAnisotropy = (int)f;
    }

    //Print some info
    LOG(LOG_VERBOSE, "Width: %i Height:%i \n", config.framebuffer.width, config.framebuffer.height);
    LOG(LOG_VERBOSE, "[gles2n64]: Enable Runfast... \n");

    OGL_EnableRunfast();
    OGL_UpdateScale();

    //We must have a shader bound before binding any textures:
    ShaderCombiner_Init();
    ShaderCombiner_Set(EncodeCombineMode(0, 0, 0, TEXEL0, 0, 0, 0, TEXEL0, 0, 0, 0, TEXEL0, 0, 0, 0, TEXEL0));
    ShaderCombiner_Set(EncodeCombineMode(0, 0, 0, SHADE, 0, 0, 0, 1, 0, 0, 0, SHADE, 0, 0, 0, 1));

    TextureCache_Init();

    memset(OGL.triangles.vertices, 0, VERTBUFF_SIZE * sizeof(SPVertex));
    memset(OGL.triangles.elements, 0, ELEMBUFF_SIZE * sizeof(GLubyte));
    OGL.triangles.num = 0;

#ifdef __TRIBUFFER_OPT
    __indexmap_init();
#endif

    OGL.frameSkipped = 0;
    for(int i = 0; i < OGL_FRAMETIME_NUM; i++) OGL.frameTime[i] = 0;

    OGL.renderingToTexture = false;
    OGL.renderState = RS_NONE;
    gSP.changed = gDP.changed = 0xFFFFFFFF;
    VI.displayNum = 0;

    return TRUE;
}

void OGL_Stop()
{
    LOG(LOG_MINIMAL, "Stopping OpenGL\n");

#ifdef USE_SDL
    SDL_QuitSubSystem( SDL_INIT_VIDEO );
#endif
	CoreVideo_Quit();

    if (config.framebuffer.enable)
    {
        glDeleteFramebuffers(1, &OGL.framebuffer.fb);
		OPENGL_CHECK_ERRORS;
        glDeleteTextures(1, &OGL.framebuffer.color_buffer);
		OPENGL_CHECK_ERRORS;
        glDeleteRenderbuffers(1, &OGL.framebuffer.depth_buffer);
		OPENGL_CHECK_ERRORS;
    }

    glDeleteShader(OGL.defaultFragShader);
	OPENGL_CHECK_ERRORS;

    glDeleteShader(OGL.defaultVertShader);
	OPENGL_CHECK_ERRORS;

    glDeleteProgram(OGL.defaultProgram);
	OPENGL_CHECK_ERRORS;

    ShaderCombiner_Destroy();
    TextureCache_Destroy();
}

void OGL_UpdateCullFace()
{
    if (config.enableFaceCulling && (gSP.geometryMode & G_CULL_BOTH))
    {
        glEnable( GL_CULL_FACE );
		OPENGL_CHECK_ERRORS;
        if ((gSP.geometryMode & G_CULL_BACK) && (gSP.geometryMode & G_CULL_FRONT))
		{
            glCullFace(GL_FRONT_AND_BACK);	
			OPENGL_CHECK_ERRORS;
		}
        else if (gSP.geometryMode & G_CULL_BACK)
		{
            glCullFace(GL_BACK);
			OPENGL_CHECK_ERRORS;
		}
        else
		{
            glCullFace(GL_FRONT);
			OPENGL_CHECK_ERRORS;
		}
    }
    else
	{
        glDisable(GL_CULL_FACE);
		OPENGL_CHECK_ERRORS;
	}
}

void OGL_UpdateViewport()
{
    int x, y, w, h;
    x = config.framebuffer.xpos + (int)(gSP.viewport.x * OGL.scaleX);
    y = config.framebuffer.ypos + (int)((VI.height - (gSP.viewport.y + gSP.viewport.height)) * OGL.scaleY);
    w = (int)(gSP.viewport.width * OGL.scaleX);
    h = (int)(gSP.viewport.height * OGL.scaleY);

	DEBUG_PRINT("Video: OpenGL.cpp:%d glViewport(%d,%d,%d,%d)\n", __LINE__, x, y, w, h);
    glViewport(x, y, w, h);
	OPENGL_CHECK_ERRORS;
}

void OGL_UpdateDepthUpdate()
{
    if (gDP.otherMode.depthUpdate)
	{
		glDepthMask(GL_TRUE);
		OPENGL_CHECK_ERRORS;
	}
    else
	{
	    glDepthMask(GL_FALSE);
		OPENGL_CHECK_ERRORS;
	}
}

void OGL_UpdateScissor()
{
    int x, y, w, h;
    x = config.framebuffer.xpos + (int)(gDP.scissor.ulx * OGL.scaleX);
    y = config.framebuffer.ypos + (int)((VI.height - gDP.scissor.lry) * OGL.scaleY);
    w = (int)((gDP.scissor.lrx - gDP.scissor.ulx) * OGL.scaleX);
    h = (int)((gDP.scissor.lry - gDP.scissor.uly) * OGL.scaleY);
    glScissor(x, y, w, h);
	OPENGL_CHECK_ERRORS;
}

//copied from RICE VIDEO
void OGL_SetBlendMode()
{

    u32 blender = gDP.otherMode.l >> 16;
    u32 blendmode_1 = blender&0xcccc;
    u32 blendmode_2 = blender&0x3333;

    glEnable(GL_BLEND);
	OPENGL_CHECK_ERRORS;
    switch(gDP.otherMode.cycleType)
    {
        case G_CYC_FILL:
            glDisable(GL_BLEND);
			OPENGL_CHECK_ERRORS;
            break;

        case G_CYC_COPY:
            glBlendFunc(GL_ONE, GL_ZERO);
			OPENGL_CHECK_ERRORS;
            break;

        case G_CYC_2CYCLE:
            if (gDP.otherMode.forceBlender && gDP.otherMode.depthCompare)
            {
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
				OPENGL_CHECK_ERRORS;
                break;
            }

            switch(blendmode_1+blendmode_2)
            {
                case BLEND_PASS+(BLEND_PASS>>2):    // In * 0 + In * 1
                case BLEND_FOG_APRIM+(BLEND_PASS>>2):
                case BLEND_FOG_MEM_FOG_MEM + (BLEND_OPA>>2):
                case BLEND_FOG_APRIM + (BLEND_OPA>>2):
                case BLEND_FOG_ASHADE + (BLEND_OPA>>2):
                case BLEND_BI_AFOG + (BLEND_OPA>>2):
                case BLEND_FOG_ASHADE + (BLEND_NOOP>>2):
                case BLEND_NOOP + (BLEND_OPA>>2):
                case BLEND_NOOP4 + (BLEND_NOOP>>2):
                case BLEND_FOG_ASHADE+(BLEND_PASS>>2):
                case BLEND_FOG_3+(BLEND_PASS>>2):
                    glDisable(GL_BLEND);
					OPENGL_CHECK_ERRORS;
                    break;

                case BLEND_PASS+(BLEND_OPA>>2):
                    if (gDP.otherMode.cvgXAlpha && gDP.otherMode.alphaCvgSel)
                    {
						glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
						OPENGL_CHECK_ERRORS;
                    }
					else
					{
                        glDisable(GL_BLEND);
						OPENGL_CHECK_ERRORS;
                    }
					break;

                case BLEND_PASS + (BLEND_XLU>>2):
                case BLEND_FOG_ASHADE + (BLEND_XLU>>2):
                case BLEND_FOG_APRIM + (BLEND_XLU>>2):
                case BLEND_FOG_MEM_FOG_MEM + (BLEND_PASS>>2):
                case BLEND_XLU + (BLEND_XLU>>2):
                case BLEND_BI_AFOG + (BLEND_XLU>>2):
                case BLEND_XLU + (BLEND_FOG_MEM_IN_MEM>>2):
                case BLEND_PASS + (BLEND_FOG_MEM_IN_MEM>>2):
                    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
					OPENGL_CHECK_ERRORS;
                    break;

                case BLEND_FOG_ASHADE+0x0301:
                    glBlendFunc(GL_SRC_ALPHA, GL_ZERO);
					OPENGL_CHECK_ERRORS;
                    break;

                case 0x0c08+0x1111:
                    glBlendFunc(GL_ZERO, GL_DST_ALPHA);
					OPENGL_CHECK_ERRORS;
                    break;

                default:
                    if (blendmode_2 == (BLEND_PASS>>2))
                    {
						glDisable(GL_BLEND);
						OPENGL_CHECK_ERRORS;
                    }
					else
					{
                        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
						OPENGL_CHECK_ERRORS;
                    }
					break;
                }
                break;

    default:

        if (gDP.otherMode.forceBlender && gDP.otherMode.depthCompare && blendmode_1 != BLEND_FOG_ASHADE )
        {
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			OPENGL_CHECK_ERRORS;
            break;
        }

        switch (blendmode_1)
        {
            case BLEND_XLU:
            case BLEND_BI_AIN:
            case BLEND_FOG_MEM:
            case BLEND_FOG_MEM_IN_MEM:
            case BLEND_BLENDCOLOR:
            case 0x00c0:
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
				OPENGL_CHECK_ERRORS;
                break;

            case BLEND_MEM_ALPHA_IN:
                glBlendFunc(GL_ZERO, GL_DST_ALPHA);
				OPENGL_CHECK_ERRORS;
                break;

            case BLEND_OPA:
                //if( options.enableHackForGames == HACK_FOR_MARIO_TENNIS )
                //{
                //   glBlendFunc(BLEND_SRCALPHA, BLEND_INVSRCALPHA);
                //}

                glDisable(GL_BLEND);
				OPENGL_CHECK_ERRORS;
                break;

            case BLEND_PASS:
            case BLEND_NOOP:
            case BLEND_FOG_ASHADE:
            case BLEND_FOG_MEM_3:
            case BLEND_BI_AFOG:
                glDisable(GL_BLEND);
				OPENGL_CHECK_ERRORS;
                break;

            case BLEND_FOG_APRIM:
                glBlendFunc(GL_ONE_MINUS_SRC_ALPHA, GL_ZERO);
				OPENGL_CHECK_ERRORS;
                break;

            case BLEND_NOOP3:
            case BLEND_NOOP5:
            case BLEND_MEM:
                glBlendFunc(GL_ZERO, GL_ONE);
				OPENGL_CHECK_ERRORS;
                break;

            default:
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
				OPENGL_CHECK_ERRORS;
        }
    }

}

void OGL_UpdateStates()
{
    if (gDP.otherMode.cycleType == G_CYC_COPY)
        ShaderCombiner_Set(EncodeCombineMode(0, 0, 0, TEXEL0, 0, 0, 0, TEXEL0, 0, 0, 0, TEXEL0, 0, 0, 0, TEXEL0));
    else if (gDP.otherMode.cycleType == G_CYC_FILL)
        ShaderCombiner_Set(EncodeCombineMode(0, 0, 0, SHADE, 0, 0, 0, 1, 0, 0, 0, SHADE, 0, 0, 0, 1));
    else
        ShaderCombiner_Set(gDP.combine.mux);

#ifdef SHADER_TEST
    ProgramSwaps += scProgramChanged;
#endif

    if (gSP.changed & CHANGED_GEOMETRYMODE)
    {
        OGL_UpdateCullFace();

        if (gSP.geometryMode & G_ZBUFFER)
        {
			glEnable(GL_DEPTH_TEST);
			OPENGL_CHECK_ERRORS;
        }
		else
		{
            glDisable(GL_DEPTH_TEST);
			OPENGL_CHECK_ERRORS;
		}
    }

    if (gDP.changed & CHANGED_CONVERT)
    {
        SC_SetUniform1f(uK4, gDP.convert.k4);
        SC_SetUniform1f(uK5, gDP.convert.k5);
    }

    if (gDP.changed & CHANGED_RENDERMODE || gDP.changed & CHANGED_CYCLETYPE)
    {
        if (gDP.otherMode.cycleType == G_CYC_1CYCLE || gDP.otherMode.cycleType == G_CYC_2CYCLE)
        {
            //glDepthFunc((gDP.otherMode.depthCompare) ? GL_GEQUAL : GL_ALWAYS);
            glDepthFunc((gDP.otherMode.depthCompare) ? GL_LESS : GL_ALWAYS);
			OPENGL_CHECK_ERRORS;

            glDepthMask((gDP.otherMode.depthUpdate) ? GL_TRUE : GL_FALSE);
			OPENGL_CHECK_ERRORS;

            if (gDP.otherMode.depthMode == ZMODE_DEC)
			{
				glEnable(GL_POLYGON_OFFSET_FILL);
				OPENGL_CHECK_ERRORS;
			}
			else
			{
				glDisable(GL_POLYGON_OFFSET_FILL);
				OPENGL_CHECK_ERRORS;
			}
        }
        else
        {
            glDepthFunc(GL_ALWAYS);
			OPENGL_CHECK_ERRORS;

			glDepthMask(GL_FALSE);
			OPENGL_CHECK_ERRORS;
        }
    }

    if ((gDP.changed & CHANGED_BLENDCOLOR) || (gDP.changed & CHANGED_RENDERMODE))
        SC_SetUniform1f(uAlphaRef, (gDP.otherMode.cvgXAlpha) ? 0.5f : gDP.blendColor.a);

    if (gDP.changed & CHANGED_SCISSOR)
        OGL_UpdateScissor();

    if (gSP.changed & CHANGED_VIEWPORT)
        OGL_UpdateViewport();

    if (gSP.changed & CHANGED_FOGPOSITION)
    {
        SC_SetUniform1f(uFogMultiplier, (float) gSP.fog.multiplier / 255.0f);
        SC_SetUniform1f(uFogOffset, (float) gSP.fog.offset / 255.0f);
    }

    if (gSP.changed & CHANGED_TEXTURESCALE)
    {
        if (scProgramCurrent->usesT0 || scProgramCurrent->usesT1)
            SC_SetUniform2f(uTexScale, gSP.texture.scales, gSP.texture.scalet);
    }

    if ((gSP.changed & CHANGED_TEXTURE) || (gDP.changed & CHANGED_TILE) || (gDP.changed & CHANGED_TMEM))
    {
        //For some reason updating the texture cache on the first frame of LOZ:OOT causes a NULL Pointer exception...
        if (scProgramCurrent)
        {
            if (scProgramCurrent->usesT0)
            {
#ifdef TEXTURECACHE_TEST
                unsigned t = ticksGetTicks();
                TextureCache_Update(0);
                TextureCacheTime += (ticksGetTicks() - t);
#else
                TextureCache_Update(0);
#endif
                SC_ForceUniform2f(uTexOffset[0], gSP.textureTile[0]->fuls, gSP.textureTile[0]->fult);
                SC_ForceUniform2f(uCacheShiftScale[0], cache.current[0]->shiftScaleS, cache.current[0]->shiftScaleT);
                SC_ForceUniform2f(uCacheScale[0], cache.current[0]->scaleS, cache.current[0]->scaleT);
                SC_ForceUniform2f(uCacheOffset[0], cache.current[0]->offsetS, cache.current[0]->offsetT);
            }
            //else TextureCache_ActivateDummy(0);

            //Note: enabling dummies makes some F-zero X textures flicker.... strange.

            if (scProgramCurrent->usesT1)
            {
#ifdef TEXTURECACHE_TEST
                unsigned t = ticksGetTicks();
                TextureCache_Update(1);
                TextureCacheTime += (ticksGetTicks() - t);
#else
                TextureCache_Update(1);
#endif
                SC_ForceUniform2f(uTexOffset[1], gSP.textureTile[1]->fuls, gSP.textureTile[1]->fult);
                SC_ForceUniform2f(uCacheShiftScale[1], cache.current[1]->shiftScaleS, cache.current[1]->shiftScaleT);
                SC_ForceUniform2f(uCacheScale[1], cache.current[1]->scaleS, cache.current[1]->scaleT);
                SC_ForceUniform2f(uCacheOffset[1], cache.current[1]->offsetS, cache.current[1]->offsetT);
            }
            //else TextureCache_ActivateDummy(1);
        }
    }

    if ((gDP.changed & CHANGED_FOGCOLOR) && config.enableFog)
        SC_SetUniform4fv(uFogColor, &gDP.fogColor.r );

    if (gDP.changed & CHANGED_ENV_COLOR)
        SC_SetUniform4fv(uEnvColor, &gDP.envColor.r);

    if (gDP.changed & CHANGED_PRIM_COLOR)
    {
        SC_SetUniform4fv(uPrimColor, &gDP.primColor.r);
        SC_SetUniform1f(uPrimLODFrac, gDP.primColor.l);
    }

    if ((gDP.changed & CHANGED_RENDERMODE) || (gDP.changed & CHANGED_CYCLETYPE))
    {
#ifndef OLD_BLENDMODE
        OGL_SetBlendMode();
#else
        if ((gDP.otherMode.forceBlender) &&
            (gDP.otherMode.cycleType != G_CYC_COPY) &&
            (gDP.otherMode.cycleType != G_CYC_FILL) &&
            !(gDP.otherMode.alphaCvgSel))
        {
            glEnable( GL_BLEND );
			OPENGL_CHECK_ERRORS;

            switch (gDP.otherMode.l >> 16)
            {
                case 0x0448: // Add
                case 0x055A:
                    glBlendFunc( GL_ONE, GL_ONE );
					OPENGL_CHECK_ERRORS;
                    break;
                case 0x0C08: // 1080 Sky
                case 0x0F0A: // Used LOTS of places
                    glBlendFunc( GL_ONE, GL_ZERO );
					OPENGL_CHECK_ERRORS;
                    break;

                case 0x0040: // Fzero
                case 0xC810: // Blends fog
                case 0xC811: // Blends fog
                case 0x0C18: // Standard interpolated blend
                case 0x0C19: // Used for antialiasing
                case 0x0050: // Standard interpolated blend
                case 0x0055: // Used for antialiasing
                    glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
					OPENGL_CHECK_ERRORS;
                    break;

                case 0x0FA5: // Seems to be doing just blend color - maybe combiner can be used for this?
                case 0x5055: // Used in Paper Mario intro, I'm not sure if this is right...
                    glBlendFunc( GL_ZERO, GL_ONE );
					OPENGL_CHECK_ERRORS;
                    break;

                default:
                    LOG(LOG_VERBOSE, "Unhandled blend mode=%x", gDP.otherMode.l >> 16);
                    glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
					OPENGL_CHECK_ERRORS;
                    break;
            }
        }
        else
        {
            glDisable( GL_BLEND );
			OPENGL_CHECK_ERRORS;
        }

        if (gDP.otherMode.cycleType == G_CYC_FILL)
        {
            glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
			OPENGL_CHECK_ERRORS;

            glEnable( GL_BLEND );
			OPENGL_CHECK_ERRORS;
        }
#endif
    }

    gDP.changed &= CHANGED_TILE | CHANGED_TMEM;
    gSP.changed &= CHANGED_TEXTURE | CHANGED_MATRIX;
}

void OGL_DrawTriangle(SPVertex *vertices, int v0, int v1, int v2)
{

}

void OGL_AddTriangle(int v0, int v1, int v2)
{
#ifdef DEBUG
	assert(OGL.triangles.num + 3 < 1024);
#endif
    OGL.triangles.elements[OGL.triangles.num++] = v0;
    OGL.triangles.elements[OGL.triangles.num++] = v1;
    OGL.triangles.elements[OGL.triangles.num++] = v2;
}

void OGL_SetColorArray()
{
    if (scProgramCurrent->usesCol)
    {
	    glEnableVertexAttribArray(SC_COLOR);
		OPENGL_CHECK_ERRORS;
	}
    else
	{
        glDisableVertexAttribArray(SC_COLOR);
		OPENGL_CHECK_ERRORS;
	}
}

void OGL_SetTexCoordArrays()
{
    if (scProgramCurrent->usesT0)
	{
        glEnableVertexAttribArray(SC_TEXCOORD0);
		OPENGL_CHECK_ERRORS;
    }
	else
	{
        glDisableVertexAttribArray(SC_TEXCOORD0);
		OPENGL_CHECK_ERRORS;
	}

    if (scProgramCurrent->usesT1)
	{
        glEnableVertexAttribArray(SC_TEXCOORD1);
		OPENGL_CHECK_ERRORS;
    }
	else
	{
        glDisableVertexAttribArray(SC_TEXCOORD1);
		OPENGL_CHECK_ERRORS;
	}
}

void OGL_DrawTriangles()
{
    if (OGL.renderingToTexture && config.ignoreOffscreenRendering)
    {
        OGL.triangles.num = 0;
        return;
    }

    if (OGL.triangles.num == 0) return;

    if ((config.updateMode == SCREEN_UPDATE_AT_1ST_PRIMITIVE) && OGL.screenUpdate)
        OGL_SwapBuffers();

    if (gSP.changed || gDP.changed)
        OGL_UpdateStates();

    if (OGL.renderState != RS_TRIANGLE || scProgramChanged)
    {
        OGL_SetColorArray();
        OGL_SetTexCoordArrays();
        glDisableVertexAttribArray(SC_TEXCOORD1);
		OPENGL_CHECK_ERRORS;
        SC_ForceUniform1f(uRenderState, RS_TRIANGLE);
    }

    if (OGL.renderState != RS_TRIANGLE)
    {
#ifdef RENDERSTATE_TEST
        StateChanges++;
#endif
        glVertexAttribPointer(SC_POSITION, 4, GL_FLOAT, GL_FALSE, sizeof(SPVertex), &OGL.triangles.vertices[0].x);
		OPENGL_CHECK_ERRORS;

        glVertexAttribPointer(SC_COLOR, 4, GL_FLOAT, GL_FALSE, sizeof(SPVertex), &OGL.triangles.vertices[0].r);
		OPENGL_CHECK_ERRORS;

        glVertexAttribPointer(SC_TEXCOORD0, 2, GL_FLOAT, GL_FALSE, sizeof(SPVertex), &OGL.triangles.vertices[0].s);
		OPENGL_CHECK_ERRORS;

        OGL_UpdateCullFace();
        OGL_UpdateViewport();
 
       	glEnable(GL_SCISSOR_TEST);
		OPENGL_CHECK_ERRORS;

        OGL.renderState = RS_TRIANGLE;
    }

    glDrawElements(GL_TRIANGLES, OGL.triangles.num, GL_UNSIGNED_BYTE, OGL.triangles.elements);
    OGL.triangles.num = 0;

#ifdef __TRIBUFFER_OPT
    __indexmap_clear();
#endif
}

void OGL_DrawLine(int v0, int v1, float width )
{
    if (OGL.renderingToTexture && config.ignoreOffscreenRendering) return;

    if ((config.updateMode == SCREEN_UPDATE_AT_1ST_PRIMITIVE) && OGL.screenUpdate)
        OGL_SwapBuffers();

    if (gSP.changed || gDP.changed)
        OGL_UpdateStates();

    if (OGL.renderState != RS_LINE || scProgramChanged)
    {
#ifdef RENDERSTATE_TEST
        StateChanges++;
#endif
        OGL_SetColorArray();
        glDisableVertexAttribArray(SC_TEXCOORD0);
		OPENGL_CHECK_ERRORS;
        glDisableVertexAttribArray(SC_TEXCOORD1);
		OPENGL_CHECK_ERRORS;
        glVertexAttribPointer(SC_POSITION, 4, GL_FLOAT, GL_FALSE, sizeof(SPVertex), &OGL.triangles.vertices[0].x);
		OPENGL_CHECK_ERRORS;
        glVertexAttribPointer(SC_COLOR, 4, GL_FLOAT, GL_FALSE, sizeof(SPVertex), &OGL.triangles.vertices[0].r);
		OPENGL_CHECK_ERRORS;

        SC_ForceUniform1f(uRenderState, RS_LINE);
        OGL_UpdateCullFace();
        OGL_UpdateViewport();
        OGL.renderState = RS_LINE;
    }

    unsigned short elem[2];
    elem[0] = v0;
    elem[1] = v1;
    glLineWidth( width * OGL.scaleX );
	OPENGL_CHECK_ERRORS;
    glDrawElements(GL_LINES, 2, GL_UNSIGNED_SHORT, elem);
}

void OGL_DrawRect( int ulx, int uly, int lrx, int lry, float *color)
{
    if (OGL.renderingToTexture && config.ignoreOffscreenRendering) return;

    if ((config.updateMode == SCREEN_UPDATE_AT_1ST_PRIMITIVE) && OGL.screenUpdate)
        OGL_SwapBuffers();

    if (gSP.changed || gDP.changed)
        OGL_UpdateStates();

    if (OGL.renderState != RS_RECT || scProgramChanged)
    {
        glDisableVertexAttribArray(SC_COLOR);
		OPENGL_CHECK_ERRORS;

        glDisableVertexAttribArray(SC_TEXCOORD0);
		OPENGL_CHECK_ERRORS;

        glDisableVertexAttribArray(SC_TEXCOORD1);
		OPENGL_CHECK_ERRORS;

        SC_ForceUniform1f(uRenderState, RS_RECT);
    }

    if (OGL.renderState != RS_RECT)
    {
#ifdef RENDERSTATE_TEST
        StateChanges++;
#endif
        glVertexAttrib4f(SC_POSITION, 0, 0, gSP.viewport.nearz, 1.0);
		OPENGL_CHECK_ERRORS;

        glVertexAttribPointer(SC_POSITION, 2, GL_FLOAT, GL_FALSE, sizeof(GLVertex), &OGL.rect[0].x);
		OPENGL_CHECK_ERRORS;

        OGL.renderState = RS_RECT;
    }

	DEBUG_PRINT("Video: OpenGL.cpp:%d glViewport(%d,%d,%d,%d)\n", __LINE__, config.framebuffer.xpos, config.framebuffer.ypos, config.framebuffer.width, config.framebuffer.height);
    glViewport(config.framebuffer.xpos, config.framebuffer.ypos, config.framebuffer.width, config.framebuffer.height );
	OPENGL_CHECK_ERRORS;

    glDisable(GL_SCISSOR_TEST);
	OPENGL_CHECK_ERRORS;

    glDisable(GL_CULL_FACE);
	OPENGL_CHECK_ERRORS;

    OGL.rect[0].x = (float) ulx * (2.0f * VI.rwidth) - 1.0;
    OGL.rect[0].y = (float) uly * (-2.0f * VI.rheight) + 1.0;
    OGL.rect[1].x = (float) (lrx+1) * (2.0f * VI.rwidth) - 1.0;
    OGL.rect[1].y = OGL.rect[0].y;
    OGL.rect[2].x = OGL.rect[0].x;
    OGL.rect[2].y = (float) (lry+1) * (-2.0f * VI.rheight) + 1.0;
    OGL.rect[3].x = OGL.rect[1].x;
    OGL.rect[3].y = OGL.rect[2].y;

    glVertexAttrib4fv(SC_COLOR, color);
	OPENGL_CHECK_ERRORS;

	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

	glEnable(GL_SCISSOR_TEST);
	OPENGL_CHECK_ERRORS;

    OGL_UpdateViewport();

}

void OGL_DrawTexturedRect( float ulx, float uly, float lrx, float lry, float uls, float ult, float lrs, float lrt, bool flip )
{
    if (config.hackBanjoTooie)
    {
        if (gDP.textureImage.width == gDP.colorImage.width &&
            gDP.textureImage.format == G_IM_FMT_CI &&
            gDP.textureImage.size == G_IM_SIZ_8b)
        {
            return;
        }
    }

    if (OGL.renderingToTexture && config.ignoreOffscreenRendering) return;

    if ((config.updateMode == SCREEN_UPDATE_AT_1ST_PRIMITIVE) && OGL.screenUpdate)
        OGL_SwapBuffers();

    if (gSP.changed || gDP.changed)
        OGL_UpdateStates();

    if (OGL.renderState != RS_TEXTUREDRECT || scProgramChanged)
    {
        glDisableVertexAttribArray(SC_COLOR);
		OPENGL_CHECK_ERRORS;

        OGL_SetTexCoordArrays();
        SC_ForceUniform1f(uRenderState, RS_TEXTUREDRECT);
    }

    if (OGL.renderState != RS_TEXTUREDRECT)
    {
#ifdef RENDERSTATE_TEST
        StateChanges++;
#endif
        glVertexAttrib4f(SC_COLOR, 0, 0, 0, 0);
		OPENGL_CHECK_ERRORS;

        glVertexAttrib4f(SC_POSITION, 0, 0, (gDP.otherMode.depthSource == G_ZS_PRIM) ? gDP.primDepth.z : gSP.viewport.nearz, 1.0);
		OPENGL_CHECK_ERRORS;

        glVertexAttribPointer(SC_POSITION, 2, GL_FLOAT, GL_FALSE, sizeof(GLVertex), &OGL.rect[0].x);
		OPENGL_CHECK_ERRORS;

        glVertexAttribPointer(SC_TEXCOORD0, 2, GL_FLOAT, GL_FALSE, sizeof(GLVertex), &OGL.rect[0].s0);
		OPENGL_CHECK_ERRORS;

        glVertexAttribPointer(SC_TEXCOORD1, 2, GL_FLOAT, GL_FALSE, sizeof(GLVertex), &OGL.rect[0].s1);
		OPENGL_CHECK_ERRORS;

        OGL.renderState = RS_TEXTUREDRECT;
    }

	DEBUG_PRINT("Video: OpenGL.cpp:%d glViewport(%d,%d,%d,%d)\n", __LINE__, config.framebuffer.xpos, config.framebuffer.ypos, config.framebuffer.width, config.framebuffer.height);
    glViewport(config.framebuffer.xpos, config.framebuffer.ypos, config.framebuffer.width, config.framebuffer.height);
	OPENGL_CHECK_ERRORS;

    glDisable(GL_CULL_FACE);
	OPENGL_CHECK_ERRORS;

    OGL.rect[0].x = (float) ulx * (2.0f * VI.rwidth) - 1.0f;
    OGL.rect[0].y = (float) uly * (-2.0f * VI.rheight) + 1.0f;
    OGL.rect[1].x = (float) (lrx) * (2.0f * VI.rwidth) - 1.0f;
    OGL.rect[1].y = OGL.rect[0].y;
    OGL.rect[2].x = OGL.rect[0].x;
    OGL.rect[2].y = (float) (lry) * (-2.0f * VI.rheight) + 1.0f;
    OGL.rect[3].x = OGL.rect[1].x;
    OGL.rect[3].y = OGL.rect[2].y;

    if (scProgramCurrent->usesT0 && cache.current[0] && gSP.textureTile[0])
    {
        OGL.rect[0].s0 = uls * cache.current[0]->shiftScaleS - gSP.textureTile[0]->fuls;
        OGL.rect[0].t0 = ult * cache.current[0]->shiftScaleT - gSP.textureTile[0]->fult;
        OGL.rect[3].s0 = (lrs + 1.0f) * cache.current[0]->shiftScaleS - gSP.textureTile[0]->fuls;
        OGL.rect[3].t0 = (lrt + 1.0f) * cache.current[0]->shiftScaleT - gSP.textureTile[0]->fult;

        if ((cache.current[0]->maskS) && !(cache.current[0]->mirrorS) && (fmod( OGL.rect[0].s0, cache.current[0]->width ) == 0.0f))
        {
            OGL.rect[3].s0 -= OGL.rect[0].s0;
            OGL.rect[0].s0 = 0.0f;
        }

        if ((cache.current[0]->maskT)  && !(cache.current[0]->mirrorT) && (fmod( OGL.rect[0].t0, cache.current[0]->height ) == 0.0f))
        {
            OGL.rect[3].t0 -= OGL.rect[0].t0;
            OGL.rect[0].t0 = 0.0f;
        }

        glActiveTexture( GL_TEXTURE0);
		OPENGL_CHECK_ERRORS;
        if ((OGL.rect[0].s0 >= 0.0f) && (OGL.rect[3].s0 <= cache.current[0]->width))
		{
            glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
			OPENGL_CHECK_ERRORS;
		}

        if ((OGL.rect[0].t0 >= 0.0f) && (OGL.rect[3].t0 <= cache.current[0]->height))
		{
            glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
			OPENGL_CHECK_ERRORS;
		}

        OGL.rect[0].s0 *= cache.current[0]->scaleS;
        OGL.rect[0].t0 *= cache.current[0]->scaleT;
        OGL.rect[3].s0 *= cache.current[0]->scaleS;
        OGL.rect[3].t0 *= cache.current[0]->scaleT;
    }

    if (scProgramCurrent->usesT1 && cache.current[1] && gSP.textureTile[1])
    {
        OGL.rect[0].s1 = uls * cache.current[1]->shiftScaleS - gSP.textureTile[1]->fuls;
        OGL.rect[0].t1 = ult * cache.current[1]->shiftScaleT - gSP.textureTile[1]->fult;
        OGL.rect[3].s1 = (lrs + 1.0f) * cache.current[1]->shiftScaleS - gSP.textureTile[1]->fuls;
        OGL.rect[3].t1 = (lrt + 1.0f) * cache.current[1]->shiftScaleT - gSP.textureTile[1]->fult;

        if ((cache.current[1]->maskS) && (fmod( OGL.rect[0].s1, cache.current[1]->width ) == 0.0f) && !(cache.current[1]->mirrorS))
        {
            OGL.rect[3].s1 -= OGL.rect[0].s1;
            OGL.rect[0].s1 = 0.0f;
        }

        if ((cache.current[1]->maskT) && (fmod( OGL.rect[0].t1, cache.current[1]->height ) == 0.0f) && !(cache.current[1]->mirrorT))
        {
            OGL.rect[3].t1 -= OGL.rect[0].t1;
            OGL.rect[0].t1 = 0.0f;
        }

        glActiveTexture( GL_TEXTURE1);
		OPENGL_CHECK_ERRORS;

        if ((OGL.rect[0].s1 == 0.0f) && (OGL.rect[3].s1 <= cache.current[1]->width)){
            glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
			OPENGL_CHECK_ERRORS;
		}

        if ((OGL.rect[0].t1 == 0.0f) && (OGL.rect[3].t1 <= cache.current[1]->height)){
            glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
			OPENGL_CHECK_ERRORS;
		}

        OGL.rect[0].s1 *= cache.current[1]->scaleS;
        OGL.rect[0].t1 *= cache.current[1]->scaleT;
        OGL.rect[3].s1 *= cache.current[1]->scaleS;
        OGL.rect[3].t1 *= cache.current[1]->scaleT;
    }

    if ((gDP.otherMode.cycleType == G_CYC_COPY) && !config.texture.forceBilinear)
    {
        glActiveTexture(GL_TEXTURE0);
		OPENGL_CHECK_ERRORS;

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
		OPENGL_CHECK_ERRORS;

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
		OPENGL_CHECK_ERRORS;
    }

    if (flip)
    {
        OGL.rect[1].s0 = OGL.rect[0].s0;
        OGL.rect[1].t0 = OGL.rect[3].t0;
        OGL.rect[1].s1 = OGL.rect[0].s1;
        OGL.rect[1].t1 = OGL.rect[3].t1;
        OGL.rect[2].s0 = OGL.rect[3].s0;
        OGL.rect[2].t0 = OGL.rect[0].t0;
        OGL.rect[2].s1 = OGL.rect[3].s1;
        OGL.rect[2].t1 = OGL.rect[0].t1;
    }
    else
    {
        OGL.rect[1].s0 = OGL.rect[3].s0;
        OGL.rect[1].t0 = OGL.rect[0].t0;
        OGL.rect[1].s1 = OGL.rect[3].s1;
        OGL.rect[1].t1 = OGL.rect[0].t1;
        OGL.rect[2].s0 = OGL.rect[0].s0;
        OGL.rect[2].t0 = OGL.rect[3].t0;
        OGL.rect[2].s1 = OGL.rect[0].s1;
        OGL.rect[2].t1 = OGL.rect[3].t1;
    }

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    OGL_UpdateViewport();
}

void OGL_ClearDepthBuffer()
{
    if (OGL.renderingToTexture && config.ignoreOffscreenRendering) return;

    if ((config.updateMode == SCREEN_UPDATE_AT_1ST_PRIMITIVE) && OGL.screenUpdate)
        OGL_SwapBuffers();

/////// paulscode, graphics bug-fixes
    glDisable( GL_SCISSOR_TEST );
	OPENGL_CHECK_ERRORS;

    glDepthMask( GL_TRUE );  // fixes side-bar graphics glitches
	OPENGL_CHECK_ERRORS;

    glClearDepthf( 1.0f );
	OPENGL_CHECK_ERRORS;

    glClearColor( 0, 0, 0, 1 );
	OPENGL_CHECK_ERRORS;

    glClear( GL_DEPTH_BUFFER_BIT );
	OPENGL_CHECK_ERRORS;

    OGL_UpdateDepthUpdate();
    glEnable( GL_SCISSOR_TEST );
	OPENGL_CHECK_ERRORS;

////////
}

void OGL_ClearColorBuffer( float *color )
{
    if (OGL.renderingToTexture && config.ignoreOffscreenRendering) return;

    if ((config.updateMode == SCREEN_UPDATE_AT_1ST_PRIMITIVE) && OGL.screenUpdate)
        OGL_SwapBuffers();

    glScissor(config.framebuffer.xpos, config.framebuffer.ypos, config.framebuffer.width, config.framebuffer.height);
	OPENGL_CHECK_ERRORS;

    glClearColor( color[0], color[1], color[2], color[3] );
	OPENGL_CHECK_ERRORS;

    glClear( GL_COLOR_BUFFER_BIT );
	OPENGL_CHECK_ERRORS;

    OGL_UpdateScissor();

}

int OGL_CheckError()
{
    GLenum e = glGetError();
    if (e != GL_NO_ERROR)
    {
        printf("GL Error: ");
        switch(e)
        {
            case GL_INVALID_ENUM:   printf("INVALID ENUM"); break;
            case GL_INVALID_VALUE:  printf("INVALID VALUE"); break;
            case GL_INVALID_OPERATION:  printf("INVALID OPERATION"); break;
            case GL_OUT_OF_MEMORY:  printf("OUT OF MEMORY"); break;
        }
        printf("\n");
        return 1;
    }
    return 0;
}

void OGL_UpdateFrameTime()
{
    unsigned ticks = ticksGetTicks();
    static unsigned lastFrameTicks = 0;
    for(int i = OGL_FRAMETIME_NUM-1; i > 0; i--) OGL.frameTime[i] = OGL.frameTime[i-1];
    OGL.frameTime[0] = ticks - lastFrameTicks;
    lastFrameTicks = ticks;
}

void OGL_SwapBuffers()
{
    //OGL_DrawTriangles();
    scProgramChanged = 0;
#if 0
    static int frames = 0;
    static unsigned lastTicks = 0;
    unsigned ticks = ticksGetTicks();

    frames++;
    if (ticks >= (lastTicks + 1000))
    {

        float fps = 1000.0f * (float) frames / (ticks - lastTicks);
        LOG(LOG_MINIMAL, "fps = %.2f \n", fps);
        LOG(LOG_MINIMAL, "skipped frame = %i of %i \n", OGL.frameSkipped, frames + OGL.frameSkipped);

        OGL.frameSkipped = 0;

#ifdef BATCH_TEST
        LOG(LOG_MINIMAL, "time spent in draw calls per frame = %.2f ms\n", (float)TotalDrawTime / frames);
        LOG(LOG_MINIMAL, "average draw calls per frame = %.0f\n", (float)TotalDrawCalls / frames);
        LOG(LOG_MINIMAL, "average vertices per draw call = %.2f\n", (float)TotalTriangles / TotalDrawCalls);
        TotalDrawCalls = 0;
        TotalTriangles = 0;
        TotalDrawTime = 0;
#endif

#ifdef SHADER_TEST
        LOG(LOG_MINIMAL, "average shader changes per frame = %f\n", (float)ProgramSwaps / frames);
        ProgramSwaps = 0;
#endif

#ifdef TEXTURECACHE_TEST
        LOG(LOG_MINIMAL, "texture cache time per frame: %.2f ms\n", (float)TextureCacheTime/ frames);
        LOG(LOG_MINIMAL, "texture cache per frame: hits=%.2f misses=%.2f\n", (float)cache.hits / frames,
                (float)cache.misses / frames);
        cache.hits = cache.misses = 0;
        TextureCacheTime = 0;

#endif
        frames = 0;
        lastTicks = ticks;
    }
#endif


#ifdef PROFILE_GBI
    u32 profileTicks = ticksGetTicks();
    static u32 profileLastTicks = 0;
    if (profileTicks >= (profileLastTicks + 5000))
    {
        LOG(LOG_MINIMAL, "GBI PROFILE DATA: %i ms \n", profileTicks - profileLastTicks);
        LOG(LOG_MINIMAL, "=========================================================\n");
        GBI_ProfilePrint(stdout);
        LOG(LOG_MINIMAL, "=========================================================\n");
        GBI_ProfileReset();
        profileLastTicks = profileTicks;
    }
#endif

    if (config.framebuffer.enable)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
		OPENGL_CHECK_ERRORS;

        glClearColor( 0, 0, 0, 1 );
		OPENGL_CHECK_ERRORS;

        glClear( GL_COLOR_BUFFER_BIT );
		OPENGL_CHECK_ERRORS;

        glUseProgram(OGL.defaultProgram);
		OPENGL_CHECK_ERRORS;

        glDisable(GL_SCISSOR_TEST);
		OPENGL_CHECK_ERRORS;

        glDisable(GL_DEPTH_TEST);
		OPENGL_CHECK_ERRORS;

		DEBUG_PRINT("Video: OpenGL.cpp:%d glViewport(%d,%d,%d,%d)\n", __LINE__, config.window.xpos, config.window.ypos, config.window.width, config.window.height);
        glViewport(config.window.xpos, config.window.ypos, config.window.width, config.window.height);
		OPENGL_CHECK_ERRORS;

        static const float vert[] =
        {
            -1.0, -1.0, +0.0, +0.0,
            +1.0, -1.0, +1.0, +0.0,
            -1.0, +1.0, +0.0, +1.0,
            +1.0, +1.0, +1.0, +1.0
        };

        glActiveTexture(GL_TEXTURE0);
		OPENGL_CHECK_ERRORS;

        glBindTexture(GL_TEXTURE_2D, OGL.framebuffer.color_buffer);
		OPENGL_CHECK_ERRORS;

        if (config.framebuffer.bilinear)
        {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			OPENGL_CHECK_ERRORS;

            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			OPENGL_CHECK_ERRORS;
        }
        else
        {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			OPENGL_CHECK_ERRORS;

            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			OPENGL_CHECK_ERRORS;
        }

        glEnableVertexAttribArray(0);
		OPENGL_CHECK_ERRORS;

        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (float*)vert);
		OPENGL_CHECK_ERRORS;

        glEnableVertexAttribArray(1);
		OPENGL_CHECK_ERRORS;

        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (float*)vert + 2);
		OPENGL_CHECK_ERRORS;

        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
		OPENGL_CHECK_ERRORS;
		
		// if emulator defined a render callback function, call it before
		// buffer swap
		if (renderCallback) (*renderCallback)(true);	
		OPENGL_CHECK_ERRORS;
		if (CoreVideo_GL_SwapBuffers) CoreVideo_GL_SwapBuffers();

        glBindFramebuffer(GL_FRAMEBUFFER, OGL.framebuffer.fb);
		OPENGL_CHECK_ERRORS;

        OGL_UpdateViewport();
        if (scProgramCurrent) glUseProgram(scProgramCurrent->program);
        OGL.renderState = RS_NONE;
    }
    else
    {
		// if emulator defined a render callback function, call it before
		// buffer swap
		if (renderCallback) (*renderCallback)(true);
		if (CoreVideo_GL_SwapBuffers) CoreVideo_GL_SwapBuffers();
    }

    OGL.screenUpdate = false;

    if (config.forceBufferClear)
    {
/////// paulscode, graphics bug-fixes
    glDisable( GL_SCISSOR_TEST );
	OPENGL_CHECK_ERRORS;

    glDepthMask( GL_TRUE );  // fixes side-bar graphics glitches
	OPENGL_CHECK_ERRORS;

    glClearDepthf( 1.0f );  // fixes missing graphics on Qualcomm Adreno
	OPENGL_CHECK_ERRORS;

    glClearColor( 0, 0, 0, 1 );
	OPENGL_CHECK_ERRORS;

    glClear( GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT );
	OPENGL_CHECK_ERRORS;

    OGL_UpdateDepthUpdate();
    glEnable( GL_SCISSOR_TEST );
	OPENGL_CHECK_ERRORS;

///////
    }

}

void OGL_ReadScreen( void * pdest, int *width, int *height )
{
	if (width != NULL)
    {
	    *width = config.framebuffer.width;
	}
    if (height != NULL)
	{
        *height = config.framebuffer.height;
	}

    if (pdest == NULL)
    {
	    return;
	}

	uint8_t* const dest = (uint8_t*)pdest;   
	const uint32_t pixelCount = config.framebuffer.width * config.framebuffer.height;
	uint8_t* temp_dest = (uint8_t*)malloc(pixelCount * 4U);

	if (temp_dest == NULL)
	{
		return;
	}

    glReadPixels( config.framebuffer.xpos, config.framebuffer.ypos, config.framebuffer.width, config.framebuffer.height, GL_RGBA, GL_UNSIGNED_BYTE, temp_dest );
	OPENGL_CHECK_ERRORS;
	// The core expects the buffer to be in GL_RGB and not pixelCount *4 in length so convert
	uint32_t j = 0U;
	for (uint32_t i = 0U; i < pixelCount*4U; i += 4U)
	{
		dest[j + 0U] = temp_dest[i + 0U];
		dest[j + 1U] = temp_dest[i + 1U];
		dest[j + 2U] = temp_dest[i + 2U];
		j += 3U;
	}
	free(temp_dest);
}

