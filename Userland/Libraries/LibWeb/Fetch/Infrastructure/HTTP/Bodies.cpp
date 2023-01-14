/*
 * Copyright (c) 2022, Linus Groh <linusg@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibJS/Runtime/PromiseCapability.h>
#include <LibWeb/Bindings/MainThreadVM.h>
#include <LibWeb/Fetch/BodyInit.h>
#include <LibWeb/Fetch/Infrastructure/HTTP/Bodies.h>
#include <LibWeb/Fetch/Infrastructure/Task.h>
#include <LibWeb/WebIDL/Promise.h>

namespace Web::Fetch::Infrastructure {

Body::Body(JS::Handle<Streams::ReadableStream> stream)
    : m_stream(move(stream))
{
}

Body::Body(JS::Handle<Streams::ReadableStream> stream, SourceType source, Optional<u64> length)
    : m_stream(move(stream))
    , m_source(move(source))
    , m_length(move(length))
{
}

// https://fetch.spec.whatwg.org/#concept-body-clone
WebIDL::ExceptionOr<Body> Body::clone() const
{
    // To clone a body body, run these steps:

    auto& vm = Bindings::main_thread_vm();
    auto& realm = *vm.current_realm();

    // FIXME: 1. Let « out1, out2 » be the result of teeing body’s stream.
    // FIXME: 2. Set body’s stream to out1.
    auto out2 = vm.heap().allocate<Streams::ReadableStream>(realm, realm);

    // 3. Return a body whose stream is out2 and other members are copied from body.
    return Body { JS::make_handle(out2), m_source, m_length };
}

// https://fetch.spec.whatwg.org/#body-fully-read
void Body::fully_read(JS::SafeFunction<void(ByteBuffer)> process_body, JS::SafeFunction<void()> process_body_error,
    Variant<Empty, JS::NonnullGCPtr<JS::Object>> task_destination) const
{
    // FIXME: 1. If taskDestination is null, then set taskDestination to the result of starting a new parallel queue.

    // 2. Let successSteps given a byte sequence bytes be to queue a fetch task to run processBody given bytes, with taskDestination.
    auto success_steps = [task_destination, process_body = move(process_body)](ByteBuffer bytes) mutable {
        queue_fetch_task(*task_destination.get<JS::NonnullGCPtr<JS::Object>>(), [process_body = move(process_body), bytes = move(bytes)] {
            process_body(move(bytes));
        });
    };

    // 3. Let errorSteps be to queue a fetch task to run processBodyError, with taskDestination.
    auto error_steps = [task_destination, process_body_error = move(process_body_error)]() mutable {
        queue_fetch_task(*task_destination.get<JS::NonnullGCPtr<JS::Object>>(), [process_body_error = move(process_body_error)]() {
            process_body_error();
        });
    };

    // FIXME: 4. Let reader be the result of getting a reader for body’s stream. If that threw an exception, then run errorSteps with that exception and return.
    // FIXME: 5. Read all bytes from reader, given successSteps and errorSteps.

    // FIXME: Implement the streams spec - this is completely made up for now :^)
    if (auto const* byte_buffer = m_source.get_pointer<ByteBuffer>()) {
        success_steps(*byte_buffer);
    } else {
        error_steps();
    }
}

// https://fetch.spec.whatwg.org/#byte-sequence-as-a-body
WebIDL::ExceptionOr<Body> byte_sequence_as_body(JS::Realm& realm, ReadonlyBytes bytes)
{
    // To get a byte sequence bytes as a body, return the body of the result of safely extracting bytes.
    auto [body, _] = TRY(safely_extract_body(realm, bytes));
    return body;
}

}
