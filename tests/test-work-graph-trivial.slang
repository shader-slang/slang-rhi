// Simple work graph test shader.
// One thread-launch node: each dispatched record spawns one thread that writes
// the record's value into outputBuffer[0].

uniform RWStructuredBuffer<uint> outputBuffer;

struct NodeRecord
{
    uint value;
};

[shader("node")]
[NodeLaunch("thread")]
void writeNode(ThreadNodeInputRecord<NodeRecord> input)
{
    outputBuffer[0] = input.Get().value;
}
