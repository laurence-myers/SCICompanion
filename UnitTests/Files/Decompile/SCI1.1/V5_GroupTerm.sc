;;; Sierra Script 1.0 - (do not remove this comment)
(script# 975)
(include sci.sh)

; A term of an and with statements before its value (Pepper's Adventures
; in Time script 230, sTalkPoorRich::changeState): the term stores 1 in a
; variable and calls a procedure, and the and tests the value of the call.
; Sierra's compiler took a group: statements in parentheses, whose value
; is the value of the last one. The cond clause has no code.
; Sierra: (cond ((and (not local0) (> param1 5) ((= local0 1) (Abs param1)))) ((not local0) (= temp0 2)))
; In the second procedure, the statement before the value is an or: its
; bt goes to the end of the or, not to the then-part of the if.
; Sierra: (if (and param1 ((or param2 param3) param3)) (= temp0 1) else (= temp0 2))
(local
	local0
)

(public
	v5GroupTerm 0
	v5GroupOrStatement 1
)

(procedure (v5GroupTerm param1 &tmp temp0)
	(asm
		lal local0
		not
		bnt next
		lsp param1
		ldi 5
		gt?
		bnt next
		ldi 1
		sal local0
		push1
		lsp param1
		callk Abs, 2
		bnt next
		jmp done
	next:
		lal local0
		not
		bnt done
		ldi 2
		sat temp0
	done:
		ret
	)
)

(procedure (v5GroupOrStatement param1 param2 param3 &tmp temp0)
	(asm
		lap param1
		bnt orElse
		lap param2
		bt orEnd
		lap param3
	orEnd:
		lap param3
		bnt orElse
		ldi 1
		sat temp0
		jmp orDone
	orElse:
		ldi 2
		sat temp0
	orDone:
		ret
	)
)
