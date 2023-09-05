
import subprocess

def opengl_params(debug):
    extensions = \
    sorted([
        # For OpenGL 4.2
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
        "GL_ARB_multi_draw_indirect",
        "GL_KHR_debug",

        # For OpenGL ES 2
        "GL_EXT_clip_control",
        "GL_EXT_debug_label",
        "GL_EXT_sRGB",
        "GL_EXT_sRGB_write_control",
        "GL_KHR_debug",
        "GL_OES_element_index_uint",
        "GL_OES_vertex_array_object",
    ])
    return {
        "out-path" : "glad",
        "api" : "gl:core=4.2,gles2",
        "extensions" : f"{','.join(extensions)}",
    }


def download_glad(params):
    glad = ["python", "-m glad", "--merge"] + [f" --{n}={v}" for (n,v) in params.items()] + ["c"]
    subprocess.call(" ".join(glad), shell=True)


if __name__ == "__main__":
    download_glad(opengl_params(False))
