# CUDA graph capture

A CUDA graph records the work submitted to a stream so that it can be launched
again later with one call. The CUDA backend can submit command buffers to a
stream that is being captured, so RHI work can be recorded into a graph next to
the application's own CUDA work.

slang-rhi does not start, end, or launch graphs. The application does that with
the CUDA API, or through a library such as PyTorch. This page describes what the
application must do for the capture to succeed and for the graph to stay valid.

## Summary

1. Start the capture on a stream of your own. If memory may be allocated or
   freed anywhere in the process while the capture is active, start it in
   relaxed mode.
2. Submit command buffers with `SubmitDesc::cudaStream` set to that stream.
3. Keep every submitted command buffer alive for as long as the graph can be
   launched.
4. Before releasing those command buffers, destroy the graph and wait for its
   launches to finish.

```cpp
cudaStreamBeginCapture(stream, cudaStreamCaptureModeRelaxed);

ComPtr<ICommandBuffer> commandBuffer = encoder->finish();
SubmitDesc submitDesc = {};
ICommandBuffer* commandBuffers[] = {commandBuffer};
submitDesc.commandBuffers = commandBuffers;
submitDesc.commandBufferCount = 1;
submitDesc.cudaStream = stream;
queue->submit(submitDesc);
retained.push_back(commandBuffer); // must outlive the graph

cudaGraph_t graph;
cudaStreamEndCapture(stream, &graph);
```

## What submit does on a capturing stream

On each submit, the queue asks CUDA whether the requested stream is being
captured (`cuStreamIsCapturing`). If it is, the commands are recorded into the
graph and nothing runs. Because there is no execution to wait for, the queue:

- does not track the command buffer as in flight, and does not hold a reference
  to it;
- creates no completion event for the submission;
- still sets the fences in `SubmitDesc::signalFences` to their values at once.
  They do not mean that the work has run.

The queue's own default stream cannot be captured. Timestamp queries are only
supported on the queue's default stream, so they cannot be recorded.

## Keeping command buffers alive

The graph's nodes point into memory owned by the command buffer: the shader
parameter data, which is copied to the device on every launch, and the device
memory it is copied to. A normal submission keeps the command buffer alive until
its work has finished. A recorded submission does not, so the application must
hold a reference to each recorded command buffer until the graph is destroyed
and no launch of it is still running.

A command buffer keeps the resources it binds alive, with one exception:
device-local buffers that are not placed in a heap. Keep those alive yourself
for as long as the graph can be launched.

When the last reference to a recorded command buffer is released, its memory is
returned for reuse immediately. Wait for the graph's launches to finish first,
for example with `ICommandQueue::waitOnHost()` after the capture has ended.

## Capture mode

CUDA restricts what other code may do while a stream is being captured. In the
default mode (global), any call that CUDA considers potentially unsafe fails
with `CUDA_ERROR_STREAM_CAPTURE_UNSUPPORTED` and invalidates the capture. That
includes allocating or freeing device memory, and querying or synchronizing the
legacy default stream, from any thread and on any stream.

The queue checks for finished work, and frees the memory of resources that are
no longer in use, from inside ordinary calls such as creating a command encoder.
It runs that check with the calling thread temporarily in relaxed mode
(`cuThreadExchangeStreamCaptureMode`), so earlier work that is still in flight
on other streams does not disturb a capture, and resources released during a
capture are freed safely.

That only covers the queue's own bookkeeping. Everything else is still subject
to the capture mode, including:

- creating a buffer, texture, or other object that allocates device memory;
- recording commands that need new memory from the device's heaps;
- memory freed by the application or another library, such as a CUDA graph
  being destroyed.

**If any memory may be allocated or freed while the capture is active, start
the capture in relaxed mode.** In relaxed mode those calls are allowed. With
the CUDA runtime that is `cudaStreamBeginCapture(stream,
cudaStreamCaptureModeRelaxed)`, with the driver API `cuStreamBeginCapture(stream,
CU_STREAM_CAPTURE_MODE_RELAXED)`, and with PyTorch
`torch.cuda.graph(graph, capture_error_mode="relaxed")`. Relaxed mode removes
CUDA's check, so it is then up to the application not to make calls that depend
on the recorded work having run.

To use the default mode, create everything the recorded commands need before
the capture starts, run the same commands once beforehand so that heaps and
caches are populated, and make sure nothing is released during the capture.

## Calls to avoid during a capture

- `ICommandQueue::waitOnHost()` and fence waits. They synchronize the context,
  which fails in every capture mode and invalidates the capture.
- Reading back results of recorded work. It has not run yet.
