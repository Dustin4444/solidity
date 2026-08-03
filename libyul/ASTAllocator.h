/*
	This file is part of solidity.

	solidity is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	solidity is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with solidity.  If not, see <http://www.gnu.org/licenses/>.
*/
// SPDX-License-Identifier: GPL-3.0
/**
 * Slab allocator for the Yul AST containers.
 */

#pragma once

#include <cstddef>
#include <memory>
#include <new>
#include <utility>
#include <vector>

namespace solidity::yul
{

/// Carves fixed-size blocks out of large slabs and recycles freed blocks through per-size-class
/// free lists. Slabs are only released when the owning thread exits, so the repeated
/// build-up/tear-down of the AST in the optimiser hands back warm memory instead of going to
/// the system allocator.
class ASTArena
{
public:
	static constexpr std::size_t granularity = 16;
	static constexpr std::size_t maxBlockSize = 512;
	static constexpr std::size_t slabSize = 1024 * 1024;

	ASTArena() = default;
	ASTArena(ASTArena const&) = delete;
	ASTArena& operator=(ASTArena const&) = delete;
	~ASTArena()
	{
		for (void* slab: m_slabs)
			::operator delete(slab);
	}

	static ASTArena& instance()
	{
		static thread_local ASTArena arena;
		return arena;
	}

	void* allocate(std::size_t _bytes)
	{
		if (_bytes > maxBlockSize)
			return ::operator new(_bytes);
		void*& head = m_freeLists[sizeClass(_bytes)];
		if (head)
		{
			void* block = head;
			head = *static_cast<void**>(block);
			return block;
		}
		return carve(blockSize(_bytes));
	}

	void deallocate(void* _block, std::size_t _bytes) noexcept
	{
		if (_bytes > maxBlockSize)
			::operator delete(_block);
		else
		{
			void*& head = m_freeLists[sizeClass(_bytes)];
			*static_cast<void**>(_block) = head;
			head = _block;
		}
	}

private:
	static std::size_t sizeClass(std::size_t _bytes) { return _bytes ? (_bytes - 1) / granularity : 0; }
	static std::size_t blockSize(std::size_t _bytes) { return (sizeClass(_bytes) + 1) * granularity; }

	void* carve(std::size_t _bytes)
	{
		if (m_remaining < _bytes)
		{
			m_slabs.push_back(::operator new(slabSize));
			m_cursor = static_cast<char*>(m_slabs.back());
			m_remaining = slabSize;
		}
		char* block = m_cursor;
		m_cursor += _bytes;
		m_remaining -= _bytes;
		return block;
	}

	std::vector<void*> m_slabs;
	char* m_cursor = nullptr;
	std::size_t m_remaining = 0;
	void* m_freeLists[maxBlockSize / granularity] = {};
};

template<typename T>
class ASTAllocator
{
public:
	using value_type = T;

	ASTAllocator() = default;
	template<typename U> constexpr ASTAllocator(ASTAllocator<U> const&) noexcept {}

	T* allocate(std::size_t _n)
	{
		static_assert(alignof(T) <= ASTArena::granularity);
		return static_cast<T*>(ASTArena::instance().allocate(_n * sizeof(T)));
	}
	void deallocate(T* _block, std::size_t _n) noexcept { ASTArena::instance().deallocate(_block, _n * sizeof(T)); }

	template<typename U> bool operator==(ASTAllocator<U> const&) const noexcept { return true; }
};

struct ASTDeleter
{
	template<typename T> void operator()(T* _node) const noexcept
	{
		_node->~T();
		ASTArena::instance().deallocate(_node, sizeof(T));
	}
};

template<typename T, typename... Args>
std::unique_ptr<T, ASTDeleter> makeASTNode(Args&&... _args)
{
	void* memory = ASTArena::instance().allocate(sizeof(T));
	try
	{
		return std::unique_ptr<T, ASTDeleter>(new (memory) T(std::forward<Args>(_args)...));
	}
	catch (...)
	{
		ASTArena::instance().deallocate(memory, sizeof(T));
		throw;
	}
}

}
