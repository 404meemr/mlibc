#include <semaphore.h>
#include <errno.h>

#include <bits/ensure.h>
#include <mlibc/debug.hpp>
#include <mlibc/sysdeps.hpp>

static constexpr unsigned int semaphoreHasWaiters = static_cast<uint32_t>(1 << 31);
static constexpr unsigned int semaphoreCountMask = static_cast<uint32_t>(1 << 31) - 1;

int sem_init(sem_t *sem, int pshared, unsigned int initial_count) {
	if (pshared) {
		mlibc::infoLogger() << "mlibc: shared semaphores are unsuppored" << frg::endlog;
		errno = ENOSYS;
		return -1;
	}

	if (initial_count > SEM_VALUE_MAX) {
		errno = EINVAL;
		return -1;
	}

	sem->__mlibc_count = initial_count;

	return 0;
}

int sem_destroy(sem_t *sem) {
	return 0;
}

int sem_wait(sem_t *sem) {
	unsigned int state = 0;

	while (1) {
		if (!(state & semaphoreCountMask)) {
			if (__atomic_compare_exchange_n(&sem->__mlibc_count, &state, semaphoreHasWaiters,
						false, __ATOMIC_ACQUIRE, __ATOMIC_ACQUIRE)) {
				if(int e = mlibc::sys_futex_wait((int *)&sem->__mlibc_count, state); e)
					__ensure(!"sys_futex_wait() failed");
			}
		} else {
			unsigned int desired = (state - 1);
			if (__atomic_compare_exchange_n(&sem->__mlibc_count, &state, desired, false,
						__ATOMIC_RELAXED, __ATOMIC_RELAXED))
				return 0;
		}
	}
}

int sem_timedwait(sem_t *, const struct timespec *) {
	__ensure(!"sem_timedwait() is unimplemented");
	__builtin_unreachable();
}

int sem_post(sem_t *sem) {
	auto old_count = __atomic_load_n(&sem->__mlibc_count, __ATOMIC_RELAXED) & semaphoreCountMask;

	if (old_count + 1 > SEM_VALUE_MAX) {
		errno = EOVERFLOW;
		return -1;
	}

	auto state = __atomic_exchange_n(&sem->__mlibc_count, old_count + 1, __ATOMIC_RELEASE);

	if (state & semaphoreHasWaiters)
		if (int e = mlibc::sys_futex_wake((int *)&sem->__mlibc_count); e)
			__ensure(!"sys_futex_wake() failed");

	return 0;
}
