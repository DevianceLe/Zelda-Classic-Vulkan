// stack commands: push, pop, peek, get, set, sizes, the limits.
// frames: 200
#include "std.zh"

void show(char32[] what, stack<int> s)
{
	printf("TS: %s: size %l full %d\n", what, s->Size, s->Full ? 1 : 0);
}

global script LREx1Ex2ItemSwitch
{
	void run()
	{
		stack<int> s = new stack<int>();
		show("new", s);
		printf("TS: empty: PopBack %d PopFront %d PeekBack %d PeekFront %d Get0 %d\n", s->PopBack(), s->PopFront(), s->PeekBack(), s->PeekFront(), s->Get(0L));
		show("after empty pops", s);
		s->PushBack(10);
		s->PushBack(20);
		s->PushBack(30);
		s->PushFront(5);
		s->PushFront(1.5);
		s->PushBack(-7);
		show("six pushed", s);
		printf("TS: order: %d %d %d %d %d %d\n", s->Get(0L), s->Get(1L), s->Get(2L), s->Get(3L), s->Get(4L), s->Get(5L));
		printf("TS: PeekBack %d PeekFront %d size %l\n", s->PeekBack(), s->PeekFront(), s->Size);
		printf("TS: Get(-1) %d Get(size) %d Get(100) %d\n", s->Get(-1L), s->Get(s->Size), s->Get(100L));
		s->Set(1L, 55);
		s->Set(-1L, 99);
		s->Set(6L, 99);
		s->Set(100L, 99);
		printf("TS: after Set: %d %d %d %d %d %d\n", s->Get(0L), s->Get(1L), s->Get(2L), s->Get(3L), s->Get(4L), s->Get(5L));
		printf("TS: PopBack %d PopFront %d\n", s->PopBack(), s->PopFront());
		printf("TS: PopBack %d PopFront %d\n", s->PopBack(), s->PopFront());
		show("after four pops", s);
		printf("TS: left: %d %d\n", s->Get(0L), s->Get(1L));
		s->Clear();
		show("cleared", s);
		printf("TS: after Clear PopBack %d\n", s->PopBack());

		// big values, both ends
		s->PushBack(214748.3647);
		s->PushBack(-214748.3648);
		s->PushFront(0.0001);
		printf("TS: extremes %d %d %d\n", s->Get(0L), s->Get(1L), s->Get(2L));
		printf("TS: long index and value: %l\n", s->Size);

		// many
		for(int i = 0; i < 5000; ++i)
			s->PushBack(i);
		for(int i = 0; i < 5000; ++i)
			s->PushFront(-i);
		show("ten thousand more", s);
		printf("TS: ends %d %d, middle %d %d\n", s->PeekFront(), s->PeekBack(), s->Get(5000L), s->Get(5003L));
		int sum = 0;
		while(s->Size > 0L)
			sum += s->PopFront() % 7;
		printf("TS: drained, sum of %%7 %d\n", sum);

		// a second stack is its own
		stack<int> t = new stack<int>();
		t->PushBack(1);
		s->PushBack(2);
		printf("TS: two stacks %l %l %d %d\n", s->Size, t->Size, s->PeekBack(), t->PeekBack());
		stack<int> same = s;
		same->PushBack(3);
		printf("TS: the same stack through another variable %l %d\n", s->Size, s->PeekBack());

		// freed, and a pointer never set
		t->Free();
		printf("TS: freed: size %l full %d pop %d peek %d get %d\n", t->Size, t->Full ? 1 : 0, t->PopBack(), t->PeekFront(), t->Get(0L));
		t->PushBack(4);
		t->Set(0L, 1);
		t->Clear();
		printf("TS: freed after pushes: size %l\n", t->Size);
		stack<int> n;
		printf("TS: null: size %l full %d pop %d\n", n->Size, n->Full ? 1 : 0, n->PopBack());
		n->PushBack(1);
		printf("TS: null after push: size %l\n", n->Size);

		// stacks of other things
		stack<bool> b = new stack<bool>();
		b->PushBack(true);
		b->PushBack(false);
		printf("TS: bools %d %d size %l\n", b->PopFront() ? 1 : 0, b->PopFront() ? 1 : 0, b->Size);
		stack<long> l = new stack<long>();
		l->PushBack(2147483647L);
		l->PushBack(-5L);
		printf("TS: longs %l %l\n", l->Get(0L), l->Get(1L));
		int arr[3] = {7, 8, 9};
		stack<int[]> sa = new stack<int[]>();
		sa->PushBack(arr);
		int back[] = sa->PeekBack();
		printf("TS: array through a stack %d %d %d\n", back[0], back[1], back[2]);

		// how many stacks can exist: all kept in an array so none is collected
		stack<int> many[300];
		int made = 0;
		for(int i = 0; i < 300; ++i)
		{
			many[i] = new stack<int>();
			many[i]->PushBack(i);
			if(many[i]->Size == 1L) ++made;
		}
		printf("TS: stacks alive at once %d (asked for 300, with 7 older)\n", made);
		printf("TS: the last good one holds %d, the first %d\n", many[made - 1]->PeekBack(), many[0]->PeekBack());
		for(int i = 0; i < 300; ++i)
			many[i]->Free();
		stack<int> again = new stack<int>();
		again->PushBack(42);
		printf("TS: after freeing them, a new one: size %l value %d\n", again->Size, again->PeekBack());

		printf("TS: DONE\n");
		while(true) Waitframe();
	}
}
