#include <test/libsolidity/util/Compiler.h>

#include <liblangutil/SourceReferenceFormatter.h>

#include <test/libsolidity/util/SoltestErrors.h>

using namespace solidity;
using namespace solidity::evmasm;
using namespace solidity::frontend::test;
using namespace solidity::langutil;

using solidity::frontend::CompilerStack;

namespace {
    /// Configures the @param _stack with the @param _input given.
    void configureStack(CompilerStack& _stack, CompilerInput const& _input)
    {
        _stack.reset();

        _stack.setSources(_input.sourceCode);
        _stack.setLibraries(_input.libraryAddresses);
        if (_input.evmVersion.has_value())
            _stack.setEVMVersion(_input.evmVersion.value());
        _stack.setEOFVersion(_input.eofVersion);
        _stack.setViaIR(_input.viaIR);
        _stack.setOptimiserSettings(_input.optimise);
        if (_input.optimiserSettings.has_value())
            _stack.setOptimiserSettings(_input.optimiserSettings.value());
        _stack.setMetadataFormat(
        	_input.metadataAppendCBOR ?
			CompilerStack::defaultMetadataFormat() :
			CompilerStack::MetadataFormat::NoMetadata
        );
        if (_input.metadataHash.has_value())
        {
	       	auto convertMetadataHash = [](MetadataHash _hash) {
		        switch (_hash) {
	       			case MetadataHash::Bzzr1:
	          			return CompilerStack::MetadataHash::Bzzr1;
	          		case MetadataHash::IPFS:
	            		return CompilerStack::MetadataHash::IPFS;
	            	case MetadataHash::None:
                    default:
	             		return CompilerStack::MetadataHash::None;
	         	};
		    };
			_stack.setMetadataHash(convertMetadataHash(_input.metadataHash.value()));
        }
        if (_input.revertStrings.has_value())
            _stack.setRevertStringBehaviour(_input.revertStrings.value());
    }

    /// @param _stack to lookup the contract in.
	/// @param _name of the contract to build the compiled contract from.
	CompiledContract getContract(CompilerStack const& _stack, std::string const& _name)
	{
		using namespace solidity::frontend;

	    /// Collect assembly items buildContract
	    std::optional<evmasm::AssemblyItems> assemblyItems;
	    std::optional<evmasm::AssemblyItems> runtimeAssemblyItems;
	    if (
	        auto const* items = _stack.assemblyItems(_name);
	        items != nullptr
	    )
	        assemblyItems = *items;
	    if (
	        auto const* items = _stack.runtimeAssemblyItems(_name);
	        items != nullptr
	    )
	        runtimeAssemblyItems = *items;

	    /// Collect event signatures
	    auto const& contract = _stack.contractDefinition(_name);
	    auto const& events = contract.events();
	    auto const& interfaceEvents = contract.usedInterfaceEvents();

	    auto toSignature = [](EventDefinition const* _event) {
	        soltestAssert(_event);
	        return CompiledContract::Event{*_event};
	    };

	    auto eventSignatures = ranges::views::concat(events, interfaceEvents) |
	        ranges::views::transform(toSignature) |
	        ranges::to<std::vector>();

	    return CompiledContract{
	        _name,
	        _stack.object(_name).bytecode,
	        _stack.runtimeObject(_name).bytecode,
	        !_stack.object(_name).linkReferences.empty(),
	        _stack.cborMetadata(_name),
	        assemblyItems,
	        runtimeAssemblyItems,
	        _stack.metadata(_name),
	        _stack.contractABI(_name),
	        _stack.interfaceSymbols(_name),
	        eventSignatures
	    };
	}

    /// @returns a formatted output of all errors that occurred during
	/// compilation.
    std::string formattedStackErrors(CompilerStack const& _stack)
    {
        auto formatError = [&](auto const& _error) {
            return SourceReferenceFormatter::formatErrorInformation(*_error, _stack, true, false);
        };

        return ranges::fold_left(
            _stack.errors() | ranges::views::transform(formatError) | ranges::to<std::vector>(),
            std::string{},
            [](std::string _acc, std::string _error) { return _acc.append(_error); }
        );
    }
}

CompiledContract const* CompilerOutput::contract(ContractName const& _name) const
{
	auto const [sourceName, contractName] = std::pair{_name.source(), _name.contract()};

	auto source = m_sourceUnits.find(std::string{_name.source()});
	if (source == m_sourceUnits.end())
	    return nullptr;

	auto const& contracts = source->second;
	if (contractName.empty())
    	return contracts.empty() ? nullptr : &contracts.back();

	auto contract = ranges::find_if(contracts, [&](auto const& _c) { return _c.name == _name; });
	return (contract != contracts.end()) ? &*contract : nullptr;
}

std::vector<CompiledContract const*> CompilerOutput::contracts() const
{
	 return m_sourceUnits |
	     ranges::views::values |
	     ranges::views::join |
	     ranges::views::transform([](auto const& contract) { return &contract; }) |
	     ranges::to<std::vector>();
}

CompilerOutput const& Compiler::compile(CompilerInput const& _input)
{
    // Configure, compile & assemble output
    configureStack(m_stack, _input);
    bool success = m_stack.compile();

    auto toSourceUnit = [&](auto const& _sourceName) {
        auto contractDefinitions = m_stack.contractDefinitions(_sourceName);
        auto sourceContracts = contractDefinitions |
            ranges::views::transform([&](auto const* _contract) {
                return getContract(m_stack, _contract->fullyQualifiedName());
            }) |
            ranges::to<std::vector>();

        return std::make_pair(_sourceName, sourceContracts);
    };

    auto sourceNames = m_stack.sourceNames();
    auto contracts = sourceNames |
        ranges::views::transform(toSourceUnit) |
        ranges::to<SourceUnits>();

    m_output.emplace(CompilerOutput{
        std::move(contracts),
        success,
        m_stack.errors()
    });

    return this->output();
}

CompilerOutput const& Compiler::output() const
{
    solAssert(m_output.has_value(), "No output found. Please compile first.");
    return m_output.value();
}

void Compiler::printErrors() const
{
    std::cout << formattedStackErrors(m_stack) << std::endl;
}
