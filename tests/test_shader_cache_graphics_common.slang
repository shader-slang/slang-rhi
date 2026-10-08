// Keep this helper in an imported module so SeparateEntryPointCompilation must link each composed
// entry-point program before asking Slang for target code. Composition alone leaves the call as an
// unresolved external.
public float3 getShaderCacheTestColor()
{
    return float3(1.0, 0.0, 0.0);
}
