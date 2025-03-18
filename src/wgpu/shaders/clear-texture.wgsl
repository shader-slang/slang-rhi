struct Params {
    width: u32,
    height: u32,
    depth: u32,
    layer: u32,
    mipLevel: u32,
    format: u32,
}

struct ClearValue {
    value: vec4<f32>,
}

@group(0) @binding(1) var<uniform> params: Params;
@group(0) @binding(1) var<uniform> clearValue: ClearValue;

// Helper function to check if coordinates are within bounds
fn is_in_bounds(x: u32, y: u32, z: u32) -> bool {
    return x < params.width && y < params.height && z < params.depth;
}

// Helper function to convert float to uint
fn float_to_uint(value: f32) -> u32 {
    return bitcast<u32>(value);
}

// Helper function to convert uint to float
fn uint_to_float(value: u32) -> f32 {
    return bitcast<f32>(value);
}

// Helper function to convert float to sint
fn float_to_sint(value: f32) -> i32 {
    return i32(value);
}

// Helper function to convert sint to float
fn sint_to_float(value: i32) -> f32 {
    return f32(value);
}

// Common workgroup sizes
const WORKGROUP_SIZE_1D: u32 = 256u;
const WORKGROUP_SIZE_2D_X: u32 = 32u;
const WORKGROUP_SIZE_2D_Y: u32 = 32u;
const WORKGROUP_SIZE_3D_X: u32 = 8u;
const WORKGROUP_SIZE_3D_Y: u32 = 8u;
const WORKGROUP_SIZE_3D_Z: u32 = 8u;

// Format constants
const FORMAT_RGBA8UNORM: u32 = 0u;
const FORMAT_RGBA8SNORM: u32 = 1u;
const FORMAT_RGBA8UINT: u32 = 2u;
const FORMAT_RGBA8SINT: u32 = 3u;
const FORMAT_RGBA16FLOAT: u32 = 4u;
const FORMAT_R32UINT: u32 = 5u;
const FORMAT_R32SINT: u32 = 6u;
const FORMAT_RG32UINT: u32 = 7u;
const FORMAT_RG32SINT: u32 = 8u;
const FORMAT_RGBA32UINT: u32 = 9u;
const FORMAT_RGBA32SINT: u32 = 10u;
const FORMAT_RGBA32FLOAT: u32 = 11u;

// Storage texture bindings for different formats
@group(0) @binding(0) var output_texture_rgba8unorm: texture_storage_2d<rgba8unorm, write>;
@group(0) @binding(0) var output_texture_rgba8snorm: texture_storage_2d<rgba8snorm, write>;
@group(0) @binding(0) var output_texture_rgba8uint: texture_storage_2d<rgba8uint, write>;
@group(0) @binding(0) var output_texture_rgba8sint: texture_storage_2d<rgba8sint, write>;
@group(0) @binding(0) var output_texture_rgba16float: texture_storage_2d<rgba16float, write>;
@group(0) @binding(0) var output_texture_r32uint: texture_storage_2d<r32uint, write>;
@group(0) @binding(0) var output_texture_r32sint: texture_storage_2d<r32sint, write>;
@group(0) @binding(0) var output_texture_rg32uint: texture_storage_2d<rg32uint, write>;
@group(0) @binding(0) var output_texture_rg32sint: texture_storage_2d<rg32sint, write>;
@group(0) @binding(0) var output_texture_rgba32uint: texture_storage_2d<rgba32uint, write>;
@group(0) @binding(0) var output_texture_rgba32sint: texture_storage_2d<rgba32sint, write>;
@group(0) @binding(0) var output_texture_rgba32float: texture_storage_2d<rgba32float, write>;

// Helper function to store value to texture based on format
fn store_value(coords: vec2<i32>, value: vec4<f32>) {
    switch (params.format) {
        case FORMAT_RGBA8UNORM: {
            textureStore(output_texture_rgba8unorm, coords, value);
        }
        case FORMAT_RGBA8SNORM: {
            textureStore(output_texture_rgba8snorm, coords, value);
        }
        case FORMAT_RGBA8UINT: {
            let uintValue = vec4<u32>(
                float_to_uint(value.x),
                float_to_uint(value.y),
                float_to_uint(value.z),
                float_to_uint(value.w)
            );
            textureStore(output_texture_rgba8uint, coords, uintValue);
        }
        case FORMAT_RGBA8SINT: {
            let sintValue = vec4<i32>(
                float_to_sint(value.x),
                float_to_sint(value.y),
                float_to_sint(value.z),
                float_to_sint(value.w)
            );
            textureStore(output_texture_rgba8sint, coords, sintValue);
        }
        case FORMAT_RGBA16FLOAT: {
            textureStore(output_texture_rgba16float, coords, value);
        }
        case FORMAT_R32UINT: {
            let uintValue = vec4<u32>(
                float_to_uint(value.x),
                0u,
                0u,
                0u
            );
            textureStore(output_texture_r32uint, coords, uintValue);
        }
        case FORMAT_R32SINT: {
            let sintValue = vec4<i32>(
                float_to_sint(value.x),
                0,
                0,
                0
            );
            textureStore(output_texture_r32sint, coords, sintValue);
        }
        case FORMAT_RG32UINT: {
            let uintValue = vec4<u32>(
                float_to_uint(value.x),
                float_to_uint(value.y),
                0u,
                0u
            );
            textureStore(output_texture_rg32uint, coords, uintValue);
        }
        case FORMAT_RG32SINT: {
            let sintValue = vec4<i32>(
                float_to_sint(value.x),
                float_to_sint(value.y),
                0,
                0
            );
            textureStore(output_texture_rg32sint, coords, sintValue);
        }
        case FORMAT_RGBA32UINT: {
            let uintValue = vec4<u32>(
                float_to_uint(value.x),
                float_to_uint(value.y),
                float_to_uint(value.z),
                float_to_uint(value.w)
            );
            textureStore(output_texture_rgba32uint, coords, uintValue);
        }
        case FORMAT_RGBA32SINT: {
            let sintValue = vec4<i32>(
                float_to_sint(value.x),
                float_to_sint(value.y),
                float_to_sint(value.z),
                float_to_sint(value.w)
            );
            textureStore(output_texture_rgba32sint, coords, sintValue);
        }
        default: {
            textureStore(output_texture_rgba32float, coords, value);
        }
    }
}

// 1D texture clear
@compute @workgroup_size(WORKGROUP_SIZE_1D, 1, 1)
fn clear_1d_float(@builtin(global_invocation_id) global_id: vec3<u32>) {
    if (!is_in_bounds(global_id.x, 0u, 0u)) {
        return;
    }
    store_value(vec2<i32>(i32(global_id.x), 0), clearValue.value);
}

@compute @workgroup_size(WORKGROUP_SIZE_1D, 1, 1)
fn clear_1d_uint(@builtin(global_invocation_id) global_id: vec3<u32>) {
    if (!is_in_bounds(global_id.x, 0u, 0u)) {
        return;
    }
    store_value(vec2<i32>(i32(global_id.x), 0), clearValue.value);
}

// 1D array texture clear
@compute @workgroup_size(WORKGROUP_SIZE_1D, 1, 1)
fn clear_1d_array_float(@builtin(global_invocation_id) global_id: vec3<u32>) {
    if (!is_in_bounds(global_id.x, 0u, 0u)) {
        return;
    }
    store_value(vec2<i32>(i32(global_id.x), i32(params.layer)), clearValue.value);
}

@compute @workgroup_size(WORKGROUP_SIZE_1D, 1, 1)
fn clear_1d_array_uint(@builtin(global_invocation_id) global_id: vec3<u32>) {
    if (!is_in_bounds(global_id.x, 0u, 0u)) {
        return;
    }
    store_value(vec2<i32>(i32(global_id.x), i32(params.layer)), clearValue.value);
}

// 2D texture clear
@compute @workgroup_size(WORKGROUP_SIZE_2D_X, WORKGROUP_SIZE_2D_Y, 1)
fn clear_2d_float(@builtin(global_invocation_id) global_id: vec3<u32>) {
    if (!is_in_bounds(global_id.x, global_id.y, 0u)) {
        return;
    }
    store_value(vec2<i32>(i32(global_id.x), i32(global_id.y)), clearValue.value);
}

@compute @workgroup_size(WORKGROUP_SIZE_2D_X, WORKGROUP_SIZE_2D_Y, 1)
fn clear_2d_uint(@builtin(global_invocation_id) global_id: vec3<u32>) {
    if (!is_in_bounds(global_id.x, global_id.y, 0u)) {
        return;
    }
    store_value(vec2<i32>(i32(global_id.x), i32(global_id.y)), clearValue.value);
}

// 2D array texture clear
@compute @workgroup_size(WORKGROUP_SIZE_2D_X, WORKGROUP_SIZE_2D_Y, 1)
fn clear_2d_array_float(@builtin(global_invocation_id) global_id: vec3<u32>) {
    if (!is_in_bounds(global_id.x, global_id.y, 0u)) {
        return;
    }
    store_value(vec2<i32>(i32(global_id.x), i32(global_id.y)), clearValue.value);
}

@compute @workgroup_size(WORKGROUP_SIZE_2D_X, WORKGROUP_SIZE_2D_Y, 1)
fn clear_2d_array_uint(@builtin(global_invocation_id) global_id: vec3<u32>) {
    if (!is_in_bounds(global_id.x, global_id.y, 0u)) {
        return;
    }
    store_value(vec2<i32>(i32(global_id.x), i32(global_id.y)), clearValue.value);
}

// 3D texture clear
@compute @workgroup_size(WORKGROUP_SIZE_3D_X, WORKGROUP_SIZE_3D_Y, WORKGROUP_SIZE_3D_Z)
fn clear_3d_float(@builtin(global_invocation_id) global_id: vec3<u32>) {
    if (!is_in_bounds(global_id.x, global_id.y, global_id.z)) {
        return;
    }
    store_value(vec2<i32>(i32(global_id.x), i32(global_id.y)), clearValue.value);
}

@compute @workgroup_size(WORKGROUP_SIZE_3D_X, WORKGROUP_SIZE_3D_Y, WORKGROUP_SIZE_3D_Z)
fn clear_3d_uint(@builtin(global_invocation_id) global_id: vec3<u32>) {
    if (!is_in_bounds(global_id.x, global_id.y, global_id.z)) {
        return;
    }
    store_value(vec2<i32>(i32(global_id.x), i32(global_id.y)), clearValue.value);
}

// Cube texture clear
@compute @workgroup_size(WORKGROUP_SIZE_2D_X, WORKGROUP_SIZE_2D_Y, 1)
fn clear_cube_float(@builtin(global_invocation_id) global_id: vec3<u32>) {
    if (!is_in_bounds(global_id.x, global_id.y, 0u)) {
        return;
    }
    store_value(vec2<i32>(i32(global_id.x), i32(global_id.y)), clearValue.value);
}

@compute @workgroup_size(WORKGROUP_SIZE_2D_X, WORKGROUP_SIZE_2D_Y, 1)
fn clear_cube_uint(@builtin(global_invocation_id) global_id: vec3<u32>) {
    if (!is_in_bounds(global_id.x, global_id.y, 0u)) {
        return;
    }
    store_value(vec2<i32>(i32(global_id.x), i32(global_id.y)), clearValue.value);
}

// Cube array texture clear
@compute @workgroup_size(WORKGROUP_SIZE_2D_X, WORKGROUP_SIZE_2D_Y, 1)
fn clear_cube_array_float(@builtin(global_invocation_id) global_id: vec3<u32>) {
    if (!is_in_bounds(global_id.x, global_id.y, 0u)) {
        return;
    }
    store_value(vec2<i32>(i32(global_id.x), i32(global_id.y)), clearValue.value);
}

@compute @workgroup_size(WORKGROUP_SIZE_2D_X, WORKGROUP_SIZE_2D_Y, 1)
fn clear_cube_array_uint(@builtin(global_invocation_id) global_id: vec3<u32>) {
    if (!is_in_bounds(global_id.x, global_id.y, 0u)) {
        return;
    }
    store_value(vec2<i32>(i32(global_id.x), i32(global_id.y)), clearValue.value);
}
