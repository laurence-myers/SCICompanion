;;; Sierra Script 1.0 - (do not remove this comment)
(script# 937)
(include sci.sh)

; A while loop that is the first statement of a repeat, so the two loops
; start at the same address and each has its own jumps back to it. The while
; has a breakif (a "bt" to the exit of the while) and, at the end of its
; body, an if whose "bnt" goes straight back to the head (Sierra's compiler
; sends a jump to a jump on to the final target). As one loop, this does not
; structure; as a while nested in a repeat, it does.
; Before the repeat, a while with a continue has two jumps back to its head
; too, but it is one loop: its exit is past both of them.
; Shape from King's Quest V, script 755 (setControls::doit).
(public
	f15SharedLoopHead 0
)

(procedure (f15SharedLoopHead param1 &tmp temp0 temp1 temp2)
	(asm
	count:
		lst temp2
		ldi 3
		lt?
		bnt head
		+at temp2
		lap param1
		bnt countMore
		jmp count
	countMore:
		lat temp2
		sat temp1
		jmp count
	head:
		lst temp0
		ldi 10
		lt?
		bnt whileDone
		lap param1
		bt whileDone
		+at temp0
		lat temp1
		bnt head
		+at temp1
		jmp head
	whileDone:
		lst temp1
		ldi 5
		gt?
		bt done
		+at temp1
		jmp head
	done:
		ret
	)
)
