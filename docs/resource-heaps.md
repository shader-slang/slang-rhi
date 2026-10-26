# Resource heaps

For backend support, see the [API implementation status](api.md#iresourceheap-interface).

## Placement

Query `getBufferMemoryRequirements` or `getTextureMemoryRequirements`, then pass the
returned requirements to `createResourceHeap`. Set `BufferDesc::heap` / `TextureDesc::heap`
and `heapOffset` to place a resource at an aligned offset within that heap. A null heap
uses the normal committed allocation path. Requirements include opaque device-local
compatibility data; pass them unchanged rather than constructing or persisting them.

## Lifetime and initialization

The resource retains its heap allocation through GPU completion. The heap fields are
creation-only and are cleared in `getDesc()`, so copying that descriptor does not
implicitly create an overlapping allocation. Initial data writes the heap immediately
and must not overlap an active resource.

## Activation and aliasing

Without initial data, activate the resource using `aliasResources` before use.
Switching aliases deactivates all resources overlapping
the destination and makes its contents undefined. A destination spanning several previous
allocations retires all of them on its first activation; repeated calls naming the other
predecessors are allowed. Cross-queue synchronization remains the application's responsibility.

`aliasResources(beforeCount, before, afterCount, after)` accepts two resource arrays
and their counts. The arrays describe sets rather than corresponding pairs: a batch
can merge several previous allocations into one destination, split one into several,
or span multiple heaps.
All overlapping predecessors are retired before any destination is activated.

The `after` array must be non-empty, and its resources must not overlap each other.
The `before` array may be empty for first activation; each listed predecessor must overlap
at least one destination in the same heap. All entries must be placed buffers or textures
on the encoder's device. The predecessor list does not restrict which overlapping
resources are deactivated. Both arrays are consumed during the call,
and the command buffer retains all referenced resources.

The two-argument `aliasResources(before, after)` overload is a convenience wrapper for
one destination and an optional predecessor.

## Backend limitations

On Vulkan portability devices, placed 3D render-target textures require the enabled
`imageView2DOn3DImage` feature. The library enables it when supported on devices it creates.
This placement is unavailable on imported portability devices, whose enabled features
cannot be determined from the native handles.
