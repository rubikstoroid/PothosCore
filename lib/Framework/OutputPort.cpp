// Copyright (c) 2014-2018 Josh Blum
//                    2020 Nicholas Corgan
// SPDX-License-Identifier: BSL-1.0

#include <Pothos/Framework/OutputPortImpl.hpp>
#include "Framework/WorkerActor.hpp"
#include <Pothos/Object/Containers.hpp>

Pothos::OutputPort::OutputPort(void):
    _actor(nullptr),
    _isSignal(false),
    _index(-1),
    _elements(0),
    _totalElements(0),
    _totalBuffers(0),
    _totalLabels(0),
    _totalMessages(0),
    _pendingElements(0),
    _reserveElements(0),
    _workEvents(0),
    _readBeforeWritePort(nullptr),
    _bufferFromManager(false)
{
    this->tokenManagerInit();
}

Pothos::OutputPort::~OutputPort(void)
{
    return;
}

const std::string &Pothos::OutputPort::alias(void) const
{
    if (_alias.empty()) return this->name();
    return _alias;
}

void Pothos::OutputPort::setAlias(const std::string &alias)
{
    _alias = alias;
}

Pothos::BufferChunk Pothos::OutputPort::getBuffer(const DType &dtype, const size_t numElements)
{
    const size_t numBytes = numElements*dtype.size();

    //use the front buffer if possible
    if (_buffer.length >= numBytes)
    {
        _buffer.length = numBytes;
        if (_bufferFromManager)
        {
            std::lock_guard<Util::SpinLock> lock(_bufferManagerLock);
            _bufferManager->pop(_buffer.length);
        }
        return std::move(_buffer);
    }

    //otherwise use the internal pool
    auto out = _bufferPool.get(numBytes);
    out.length = numBytes;
    out.dtype = dtype;
    return out;
}

void Pothos::OutputPort::_postMessage(const Object &async)
{
    const auto token = this->tokenManagerPop();
    for (const auto &subscriber : _subscribers)
    {
        if (subscriber->isSlot() and async.type() == typeid(ObjectVector))
        {
            subscriber->slotCallsPush(async, token);
        }
        else
        {
            subscriber->asyncMessagesPush(async, token);
        }
    }
    _totalMessages++;
    _workEvents++;
}

void Pothos::OutputPort::bufferManagerPush(Pothos::Util::SpinLock *mutex, const Pothos::ManagedBuffer &buff)
{
    {
        std::lock_guard<Pothos::Util::SpinLock> lock(*mutex);
        buff.getBufferManager()->push(buff);
    }
    assert(_actor != nullptr);
    _actor->flagExternalChange();
}

void Pothos::OutputPort::bufferManagerSetup(const Pothos::BufferManager::Sptr &manager)
{
    std::lock_guard<Util::SpinLock> lock(_bufferManagerLock);
    _bufferManager = manager;
    if (manager) manager->setCallback(std::bind(
        &Pothos::OutputPort::bufferManagerPush, this, &_bufferManagerLock, std::placeholders::_1));
}

void Pothos::OutputPort::tokenManagerInit(void)
{
    BufferManagerArgs tokenMgrArgs;
    tokenMgrArgs.numBuffers = 16;
    tokenMgrArgs.bufferSize = 0;
    _tokenManager = BufferManager::make("generic", tokenMgrArgs);
    _tokenManager->setCallback(std::bind(
        &Pothos::OutputPort::bufferManagerPush, this, &_tokenManagerLock, std::placeholders::_1));
}

#include <Pothos/Managed.hpp>

static auto managedOutputPort = Pothos::ManagedClass()
    .registerClass<Pothos::OutputPort>()
    .registerMethod(POTHOS_FCN_TUPLE(Pothos::OutputPort, index))
    .registerMethod(POTHOS_FCN_TUPLE(Pothos::OutputPort, name))
    .registerMethod(POTHOS_FCN_TUPLE(Pothos::OutputPort, dtype))
    .registerMethod(POTHOS_FCN_TUPLE(Pothos::OutputPort, domain))
    .registerMethod(POTHOS_FCN_TUPLE(Pothos::OutputPort, buffer))
    .registerMethod(POTHOS_FCN_TUPLE(Pothos::OutputPort, elements))
    .registerMethod(POTHOS_FCN_TUPLE(Pothos::OutputPort, totalElements))
    .registerMethod(POTHOS_FCN_TUPLE(Pothos::OutputPort, totalMessages))
    .registerMethod(POTHOS_FCN_TUPLE(Pothos::OutputPort, produce))
    .registerMethod(POTHOS_FCN_TUPLE(Pothos::OutputPort, popElements))
    .registerMethod("getBuffer", Pothos::Callable::make<Pothos::BufferChunk, Pothos::OutputPort, size_t>(&Pothos::OutputPort::getBuffer))
    .registerMethod("getBuffer", Pothos::Callable::make<Pothos::BufferChunk, Pothos::OutputPort, const Pothos::DType &, size_t>(&Pothos::OutputPort::getBuffer))
    .registerMethod("postLabel", &Pothos::OutputPort::postLabel<const Pothos::Label &>)
    .registerMethod("postMessage", &Pothos::OutputPort::postMessage<const Pothos::Object &>)
    .registerMethod("postBuffer", &Pothos::OutputPort::postBuffer<const Pothos::BufferChunk &>)
    .registerMethod(POTHOS_FCN_TUPLE(Pothos::OutputPort, setReserve))
    .registerMethod(POTHOS_FCN_TUPLE(Pothos::OutputPort, isSignal))
    .registerMethod(POTHOS_FCN_TUPLE(Pothos::OutputPort, setReadBeforeWrite))
    .commit("Pothos/OutputPort");

/***********************************************************************
 * Register toString() outputs
 **********************************************************************/

#include <Pothos/Object/RegisterToString.hpp>
#include <Pothos/Plugin.hpp>

static std::string pothosOutputPortToString(const Pothos::OutputPort& outputPort)
{
    return "Pothos::OutputPort (alias: " + outputPort.alias() +  ", dtype: " + outputPort.dtype().toString() + ")";
}

pothos_static_block(pothosRegisterOutputPortToString)
{
    Pothos::registerToStringFunc<Pothos::OutputPort>(
        "Pothos/OutputPort",
        &pothosOutputPortToString,
        true /*registerPointerTypes*/);
}
