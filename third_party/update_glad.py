
import subprocess

def download_glad(extensions, debug):
    params = {
        "out-path" : "glad",
        "profile" : "core",
        "api" : "gl=4.2",
        "generator" : "c-debug" if debug else "c",
        "spec" : "gl",
        "extensions" : f"{','.join(extensions)}",
    }
    glad = ["python", "-m glad", "--local-files"] + [f" --{n}={v}" for (n,v) in params.items()]
    subprocess.call(" ".join(glad), shell=True)


if __name__ == "__main__":
    extensions = \
    sorted([
        "GL_ARB_buffer_storage",
        "GL_ARB_clear_texture",
        "GL_ARB_clip_control",
        "GL_ARB_compute_shader",
        "GL_ARB_debug_output",
        "GL_ARB_direct_state_access",
        "GL_ARB_gpu_shader5",
        "GL_ARB_program_interface_query",
        "GL_ARB_shader_image_load_store",
        "GL_ARB_shader_storage_buffer_object",
        "GL_KHR_debug",
    ])
    download_glad(extensions, False)