
import subprocess

def opengl_params(debug):
    extensions = \
    sorted([
        "GL_ARB_buffer_storage",
        "GL_ARB_clear_texture",
        "GL_ARB_clip_control",
        "GL_ARB_compute_shader",
        "GL_ARB_debug_output",
        "GL_ARB_direct_state_access",
        "GL_ARB_gpu_shader5",
        "GL_ARB_parallel_shader_compile",
        "GL_ARB_program_interface_query",
        "GL_ARB_shader_image_load_store",
        "GL_ARB_shader_storage_buffer_object",
        "GL_KHR_debug",
    ])
    return {
        "out-path" : "glad",
        "profile" : "core",
        "api" : "gl=4.2",
        "generator" : "c-debug" if debug else "c",
        "spec" : "gl",
        "extensions" : f"{','.join(extensions)}",
    }


def wgl_params(debug):
    extensions = \
    sorted([
        "WGL_EXT_extensions_string",
        "WGL_ARB_extensions_string",
        "WGL_ARB_create_context",
        "WGL_ARB_create_context_profile",
    ])
    return {
        "out-path" : "glad",
        "api" : "wgl=1.0",
        "generator" : "c-debug" if debug else "c",
        "spec" : "wgl",
        "extensions" : f"{','.join(extensions)}",
    }


def glx_params(debug):
    extensions = \
    sorted([
        "GLX_ARB_create_context",
        "GLX_ARB_create_context_profile",
    ])
    return {
        "out-path" : "glad",
        "api" : "glx=1.4",
        "generator" : "c-debug" if debug else "c",
        "spec" : "glx",
        "extensions" : f"{','.join(extensions)}",
    }


def download_glad(params):
    glad = ["python", "-m glad", "--local-files"] + [f" --{n}={v}" for (n,v) in params.items()]
    subprocess.call(" ".join(glad), shell=True)


if __name__ == "__main__":
    download_glad(wgl_params(False))
    download_glad(glx_params(False))
    download_glad(opengl_params(False))
