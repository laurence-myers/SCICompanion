;;; Sierra Script 1.0 - (do not remove this comment)
(script# 936)
(include sci.sh)

; A repeat whose body is an if-else. A break in the then branch jumps past
; the latch (the jump back to the head at the end of the then branch), so
; the follow node of the repeat is far past its latch. The else branch
; comes after the latch and holds a second repeat, which nests in the first
; one although it is not between the head and the latch of the first one.
; The break of the inner repeat jumps straight to the end: Sierra's
; compiler sends a jump to a jump on to the final target. The two jumps
; before "done" are dead code from Sierra's compiler: the break after the
; inner repeat, and the jump back to the head at the end of the repeat body.
; Shape from King's Quest V, script 755 (setControls::doit).
(public
	f14BreakPastLatch 0
)

(procedure (f14BreakPastLatch param1 &tmp temp0 temp1)
	(asm
	outer:
		lap param1
		bnt keys
		lat temp0
		bnt mouseMore
		jmp done
	mouseMore:
		ldi 1
		sat temp0
		jmp outer
	keys:
		ldi 0
		sat temp1
	count:
		lat temp0
		bnt countTest
		+at temp1
	countTest:
		lst temp1
		ldi 16
		ge?
		bnt countNext
		jmp done
	countNext:
		lat temp1
		sat temp0
		jmp count
		jmp done
		jmp outer
	done:
		ret
	)
)
