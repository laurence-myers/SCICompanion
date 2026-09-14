;;; Sierra Script 1.0 - (do not remove this comment)
(script# 924)
(include sci.sh)

; A shared "jmp head" inside a loop body. Sierra's compiler sends a jump to
; the end of the body straight to the loop head, but leaves a conditional
; branch pointed at such a jump. The bare jump is then the end of several ifs
; at once, and a second name for the loop's common latch. The decompiler must
; fold it into the common latch, or the if that contains one of those
; branches gathers the shared jump and fails with "Exit needs two
; predecessors".
(public
	f11LatchTrampoline 0
)

(procedure (f11LatchTrampoline param1 &tmp temp0 temp1)
	(asm
		ldi 0
		sat temp0
		ldi 0
		sat temp1
	loopHead:
		lst temp0
		lap param1
		lt?
		bnt loopExit
		+at temp0
		lst temp0
		ldi 1
		eq?
		bnt else1
		lst temp0
		ldi 2
		and
		bnt inner
		ldi 1
		sat temp1
		jmp loopHead
	inner:
		lst temp0
		ldi 4
		and
		bnt tramp
		ldi 2
		sat temp1
		jmp loopHead
	else1:
		lst temp0
		ldi 3
		eq?
		bnt last
		lst temp1
		ldi 0
		eq?
		bnt tramp
		ldi 3
		sat temp1
		jmp loopExit
	tramp:
		jmp loopHead
	last:
		ldi 4
		sat temp1
		jmp loopHead
	loopExit:
		lat temp1
		ret
	)
)
