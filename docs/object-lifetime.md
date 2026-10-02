# Object lifetime

RHI device children have two kinds of owning reference. Both retain the object;
only external references retain its device. Ownership is a property of each
reference, so an object can have both kinds at once.

## Choosing a reference

| Reference | Retains object | Retains device | Use |
|-----------|----------------|----------------|-----|
| `ComPtr<I...>` / `RefPtr<T>` | Yes | Yes | Application handles, factory results, work that must keep the device alive |
| `InternalRefPtr<T>` | Yes | No | Device caches, in-flight command buffers, same-device dependencies |
| Raw pointer | No | No | Borrowed access while another owner guarantees lifetime |

An internal reference is an owning reference, not a weak reference. Its containing
owner or operation must keep the device valid through use, destruction, and
promotion (acquiring an external reference from an internal one). Promotion
during device teardown is invalid.

Typical internal edges are:

```text
device -> queue -> in-flight command buffer -> resources
device -> cached pipeline -> shader program
shader object -> bound resources and subobjects
resource -> backing buffer or heap
```

These edges allow device destruction to release outstanding work and caches.
Internal reference cycles still leak. Shader entry points and automatically
created subobjects also retain their program internally, keeping its reflection
alive when returned independently of the root shader object.

## Returning objects

Receive factory results into `ComPtr` or `RefPtr`, then convert to internal
ownership as needed. `InternalRefPtr` does not support `attach`, `detach`, or
`writeRef`, which would lose the ownership kind at a raw-pointer boundary.

The [return helpers](../src/reference.h) give the caller an owned external
reference. `ComPtr` helpers return interface pointers; `RefPtr` helpers return
implementation pointers.

| Helper | Source | Effect |
|--------|--------|--------|
| `returnComPtr` | `ComPtr` or external `RefPtr` | Transfer the reference; clear the source |
| `returnRefPtr` | External `RefPtr` | Transfer the reference; clear the source |
| `returnComPtrCopy` | Raw pointer or external/internal `RefPtr` | Add an external reference; preserve the source |
| `returnRefPtrCopy` | External/internal `RefPtr` | Add an external reference; preserve the source |

Transfer local factory results to avoid an add/release pair. The object's count
need not be one: only the source's reference changes hands, and the counts stay
unchanged. Use the `Copy` variants for cached or member objects whose source
reference must remain owned.

## Reference counting

The first external reference retains the object's lifetime owner, normally its
device. Further external copies share that owner reference. The last external
release drops it after any resulting object destruction or deferred enqueue has
completed.

When both counts reach zero, `deleteThis()` destroys the object or queues deferred
deletion. Cached default views are the exception described below. Ordinary objects
cannot be reacquired once committed to deletion. A release can reentrantly destroy
the device and its children, so code must not access an object after releasing its
last protection.

## Texture views

The shared [`TextureView`](../src/rhi-shared.cpp) implementation pairs **every view
reference** with a texture reference of the same kind:

```text
external view reference -> external texture reference -> device retention
internal view reference -> internal texture reference
```

This also applies to type-erased `InternalRefPtr<RefObject>` command tracking.
External views keep both texture and device alive after the application's
texture and device handles are released.

### Cached default views

`Texture::getDefaultView()` returns an owned external reference to a lazily created,
cached view. The texture owns the view's **allocation without holding a counted
reference**. Consequently:

- Ordinary views delete themselves when their counts reach zero.
- A default view remains constructed at zero references until its texture dies.
- A dormant default view retains neither its texture nor its device, avoiding a
  texture/view reference cycle.

The cache uses an atomic pointer. Concurrent first requests publish one fully
initialized candidate; losing candidates are released normally. A backend texture
destructor calls `destroyDefaultView()` before releasing native texture storage.
An unfinished final view release still retains the texture, so destruction can
only reach a dormant cached view. `destroyDefaultView()` must not be used for
cache eviction. Ordinary views are not deferred; textures themselves may be.

The cache makes this borrowed use safe while `texture` remains retained:

```cpp
colorAttachment.view = texture->getDefaultView();
// Consume the attachment descriptor while texture is still alive.
```

Command recording then retains the view internally. The raw pointer in the
attachment descriptor itself owns no reference.

## Shutdown and deferred deletion

For D3D12, Vulkan, CUDA, and Metal, device teardown follows this order:

1. `waitAndReleaseCommandBuffers()` waits for submitted work and releases command
   buffers while their device-owned staging heaps remain valid.
2. The device releases caches and staging heaps.
3. Queue `shutdown()` drains deferred deletion before destroying native queue
   services. Descriptor allocators remain valid during the drain.

**Deferred resource destructors must not enqueue further deferred deletes:** the
queue holds its mutex throughout destruction. Release child resources that need
deferred deletion in `deleteThis()` before enqueueing the parent.

Texture samplers and D3D12/Vulkan acceleration-structure and micromap buffers are
released this way, allowing them to retire with the parent instead of acquiring
a later submission ID during its destructor. CUDA samplers are CPU-only and can
be released immediately.

Dependencies needed through native teardown, such as a placed resource's backing
heap, must remain retained until destruction. Cached default views also remain
until texture destruction because releasing their descriptor slots is immediate.

See the [D3D12 device](../src/d3d12/d3d12-device.cpp) and
[queue](../src/d3d12/d3d12-command.cpp) for the teardown order.

## Implementation notes

[`RefObject`](../src/core/smart-pointer.h) stores external and internal counts in
one atomic 64-bit word. [`DeviceChild`](../src/device-child.h) supplies its immutable
device association through `getLifetimeOwner()`; plain `RefObject` instances have
no owner by default.

The first external reference retains the owner before publishing its count. The
last external release converts itself into a temporary internal reference, saves
the owner, and drops the temporary reference before releasing the owner. Racing
external lifetimes can temporarily hold separate owner references.

Texture views use an immutable borrowed texture pointer. Each acquisition retains
the texture before incrementing the view count; each release saves the texture
pointer, releases the view locally, then releases the matching texture reference
without accessing the view again. Views return null from `getLifetimeOwner()`
because their texture provides device retention. The temporary internal guard in
`RefObject::releaseReference()` uses a qualified base-class release so it does not
forward an unmatched release to the texture.

[Counter and view tests](../tests/test-ref-object.cpp) cover release orders and
concurrent transitions using the production view implementation;
[backend lifetime tests](../tests/test-device-lifetime.cpp) cover surviving views
and shader subobjects, abandoned recordings, and shutdown with submitted work.
