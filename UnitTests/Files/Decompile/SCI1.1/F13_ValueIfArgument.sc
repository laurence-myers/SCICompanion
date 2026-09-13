;;; Sierra Script 1.0 - (do not remove this comment)
(script# 931)
(include sci.sh)

; A send whose last argument is an if used as a value, with the selector
; and the first argument pushed before the if. QfG4 Str::right,
; (= str (self subStr: 0 (if (> charCount mySize) mySize else charCount))):
; the send's block after the join holds only "push; send", and the three
; pushes before the if belong to it too.
; Sierra: (= temp0 (param3 posn: 0 (if (> param1 param2) param2 else param1)))
; Two value ifs as the arguments (Str::right): the block between them
; starts with the first if's push and holds the second if's test.
; Sierra: (= temp0 (param3 posn: (if (> param1 param2) (- param2 param1) else 0)
;   (if (> param1 param2) param2 else param1)))
; The same with the compare's load dropped: the accumulator holds temp1 from
; the store before the send's pushes (Str::right, mySize). The test of the
; first if needs that accumulator, so its walk stops at the pushes: they and
; the store belong to the frames below the if, after it.
; Sierra: (= temp1 (param3 size:)) (= temp0 (param3 posn: (if (> param1 temp1)
;   (- temp1 param1) else 0) (if (> param1 temp1) temp1 else param1)))
(public
	f13ValueIfArgument 0
	f13TwoValueIfArguments 1
	f13DroppedLoadArgument 2
)

(procedure (f13ValueIfArgument param1 param2 param3 &tmp temp0)
	(asm
		pushi #posn
		push2
		push0
		lsp param1
		lap param2
		gt?
		bnt elseBranch
		lap param2
		jmp done
	elseBranch:
		lap param1
	done:
		push
		lap param3
		send 8
		sat temp0
		ret
	)
)

(procedure (f13TwoValueIfArguments param1 param2 param3 &tmp temp0)
	(asm
		pushi #posn
		push2
		lsp param1
		lap param2
		gt?
		bnt else1
		lsp param2
		lap param1
		sub
		jmp done1
	else1:
		ldi 0
	done1:
		push
		lsp param1
		lap param2
		gt?
		bnt else2
		lap param2
		jmp done2
	else2:
		lap param1
	done2:
		push
		lap param3
		send 8
		sat temp0
		ret
	)
)

(procedure (f13DroppedLoadArgument param1 param2 param3 &tmp temp0 temp1)
	(asm
		pushi #size
		push0
		lap param3
		send 4
		sat temp1
		pushi #posn
		push2
		lsp param1
		gt?
		bnt else3
		lst temp1
		lap param1
		sub
		jmp done3
	else3:
		ldi 0
	done3:
		push
		lsp param1
		lat temp1
		gt?
		bnt else4
		lat temp1
		jmp done4
	else4:
		lap param1
	done4:
		push
		lap param3
		send 8
		sat temp0
		ret
	)
)
