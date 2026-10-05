;;; Sierra Script 1.0 - (do not remove this comment)
(script# 955)
(include sci.sh)
(use System)

; The test puts branches out of a function into the code of others, as the
; patches of games do: the KQ4 copy in "patch\NEW" (script 32: a new init
; ends with a jmp to the old one, which no other code calls) and the SQ4
; copy in "patch" (script 271: methods share the tail of a switch of
; another method, and its "toss; ret").
(public
	detour 0
	switcher 1
	sharer 2
	jmpFirst 3
	backward 4
)

(local
	local0
)

; The old code: once the test sets its jmp, detour goes here, and nothing
; calls it.
(procedure (oldBody)
	(= local0 7)
	(if (== local0 8)
		(= local0 9)
	)
)

; The test puts a jmp to oldBody at the push0 of the call.
(procedure (detour)
	(= local0 300)
	(oldBody)
)

; The switch whose else and toss sharer goes to.
(procedure (switcher param1)
	(switch param1
		(1 (= local0 1))
		(else (= local0 2))
	)
)

; The test sets the bnt to the else of switcher (ldi 2), and the jmp to its
; toss.
(procedure (sharer param1)
	(asm
		lsp param1
		ldi 1
		eq?
		bnt elseHere
		ldi 3
		sal local0
		jmp tossHere
	elseHere:
		ldi 4
		sal local0
	tossHere:
		ret
	)
)

; The test sets the jmp to the toss of switcher, and then the bnt to its
; else (ldi 2), whose code goes on into that toss: the part of the toss
; moves after the part of the else.
(procedure (jmpFirst param1)
	(asm
		lsp param1
		ldi 1
		eq?
		bnt jfThen
		ldi 5
		sal local0
		jmp jfToss
	jfThen:
		lsp param1
		ldi 2
		eq?
		bnt jfElse
		ldi 6
		sal local0
	jfElse:
		ldi 7
	jfToss:
		ret
	)
)

; The test sets the bnt, before a ret, to the else of switcher: a target
; before the start of the procedure, in the code of the script.
(procedure (backward param1)
	(asm
		lsp param1
		ldi 1
		eq?
		bnt bwThere
		ret
	bwThere:
		ret
	)
)

; The tail of doit has a super of Code.
(class X7Tail of Code
	(properties)

	(method (doit)
		(= local0 5)
		(super doit:)
	)
)

; The test sets the jmp to the "pushi #doit" of X7Tail::doit: a super of
; Code, which is not the superclass of X7Other.
(class X7Other of Script
	(properties)

	(method (doit)
		(asm
			ldi 6
			sal local0
			jmp there
		there:
			ret
		)
	)
)
