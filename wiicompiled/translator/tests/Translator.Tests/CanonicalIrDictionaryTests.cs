using System.Buffers.Binary;
using System.Text.Json;
using Translator.Core.Analysis;
using Translator.Core.Ir;
using Translator.Core.Loading;
using Translator.Core.Translation;
using Xunit;

namespace Translator.Tests;

public sealed class CanonicalIrDictionaryTests
{
    [Fact]
    public void KeysAndMembershipNeverDecodeAndValuesAreNotCached()
    {
        var store = new CanonicalIrStore();
        store.Put(0x80002000, Function("second", new IrReturn(null)));
        store.Put(0x80001000, Function("first", new IrReturn(null)));
        var provider = new CountingProvider(store);
        var addresses = new uint[] { 0x80002000, 0x80001000 };
        var view = new CanonicalIrDictionary(provider, addresses);
        addresses[0] = 0;

        Assert.Equal(2, view.Count);
        Assert.Equal(new uint[] { 0x80002000, 0x80001000 }, view.Keys);
        Assert.True(view.ContainsKey(0x80001000));
        Assert.False(view.ContainsKey(0));
        Assert.False(view.TryGetValue(0, out _));
        Assert.Throws<KeyNotFoundException>(() => view[0]);
        Assert.Equal(0, provider.Reads);

        var first = view[0x80001000];
        Assert.NotSame(first, view[0x80001000]);
        Assert.Equal(2, provider.Reads);
        Assert.Single(view.Values.Take(1));
        Assert.Equal(3, provider.Reads); // Iteration does not eagerly decode every value.
        Assert.Equal(view.Keys, view.Select(pair => pair.Key));
    }

    [Fact]
    public void LazyAnalysisMatchesBufferedContractsAndComponentsAcrossRecursiveCalls()
    {
        var functions = new Dictionary<uint, IrFunction>
        {
            [0x80003000] = Function("leaf", new IrAssign("r7", IrValue.Register("r6")), new IrReturn(null)),
            [0x80001000] = Function("first",
                new IrAssign("r5", IrValue.Register("r3")),
                new IrCall("lr", "0x80002000", Array.Empty<IrValue>()), new IrReturn(null)),
            [0x80002000] = Function("second",
                new IrAssign("r6", IrValue.Register("r4")),
                new IrCall("lr", "0x80001000", Array.Empty<IrValue>()),
                new IrCall("lr", "0x80003000", Array.Empty<IrValue>()), new IrReturn(null))
        };
        var store = new CanonicalIrStore();
        foreach (var pair in functions) store.Put(pair.Key, pair.Value);
        var view = new CanonicalIrDictionary(store, functions.Keys);
        var buffered = GuestAbiInterproceduralAnalyzer.Analyze(functions);
        var lazy = GuestAbiInterproceduralAnalyzer.Analyze(view, retainBodies: false, maxDegreeOfParallelism: 2);

        Assert.Equal(JsonSerializer.Serialize(buffered.Contracts), JsonSerializer.Serialize(lazy.Contracts));
        Assert.Equal(JsonSerializer.Serialize(buffered.StronglyConnectedComponents),
            JsonSerializer.Serialize(lazy.StronglyConnectedComponents));
        Assert.Contains(lazy.StronglyConnectedComponents,
            component => component.SequenceEqual(new uint[] { 0x80001000, 0x80002000 }));
    }

    [Fact]
    public void LazyCanonicalBodyProducesIdenticalLoweredSourceAndSerializedIr()
    {
        var memory = new byte[8];
        BinaryPrimitives.WriteUInt32BigEndian(memory.AsSpan(0, 4), 0x38630001u); // addi r3,r3,1
        BinaryPrimitives.WriteUInt32BigEndian(memory.AsSpan(4, 4), 0x4E800020u); // blr
        var image = new ProgramImage(memory,
            AddressRange.FromStartAndSize(MemoryLayout.RamBase, (uint)memory.Length),
            AddressRange.FromStartAndSize(MemoryLayout.RamBase, (uint)memory.Length), default, "bounded-test");
        var translator = new FunctionTranslator(image);
        var options = TranslationOptions.Default with { PreferredName = "bounded_test", AllowUnsupportedInstructions = true };
        var canonical = translator.Discover(MemoryLayout.RamBase, options).Ssa.Function;
        var store = new CanonicalIrStore();
        store.Put(MemoryLayout.RamBase, canonical);
        var view = new CanonicalIrDictionary(store, new[] { MemoryLayout.RamBase });

        var buffered = translator.LowerCanonical(MemoryLayout.RamBase, canonical, options);
        var lazy = translator.LowerCanonical(MemoryLayout.RamBase, view[MemoryLayout.RamBase], options);
        Assert.Equal(buffered.CxxCode, lazy.CxxCode);
        Assert.Equal(JsonSerializer.Serialize(canonical), JsonSerializer.Serialize(view[MemoryLayout.RamBase]));
    }

    private static IrFunction Function(string name, params IrInstruction[] instructions) =>
        new(name, "entry", new[] { new IrBasicBlock("entry", instructions) });

    private sealed class CountingProvider(ICanonicalIrProvider inner) : ICanonicalIrProvider
    {
        public int Reads { get; private set; }
        public bool TryGet(uint entryPoint, out IrFunction function)
        {
            ++Reads;
            return inner.TryGet(entryPoint, out function);
        }
    }
}
