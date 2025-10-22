.code

;
; 用户态外部Hypercall调用约定（复用内核协议）：
; - R10-R12: 通信魔数（用于进入VmxVmcallHandler）
; - RCX (VmcallNumber): Hypercall功能代码
; - RDX (OptionalParam1): 必须是HYPERCALL_KEY（用于路由到VmmCallbackVmcallHandler）
; - R8  (OptionalParam2): 参数1
; - R9  (OptionalParam3): 参数2
;
; 工作流程：
; 1. R10-R12 magic -> 进入 VmxVmcallHandler
; 2. RDX == HYPERCALL_KEY -> 路由到 VmmCallbackVmcallHandler (外部回调)
; 3. RDX != HYPERCALL_KEY -> 执行内部VMCALL逻辑
;
; UINT64 VmxVmCall(UINT64 VmcallNumber, UINT64 OptionalParam1, UINT64 OptionalParam2, UINT64 OptionalParam3)
;
VmxVmCall proc
    ; 保存flags和R10-R12寄存器
    pushfq
    push r10
    push r11
    push r12

    ; 设置通信魔数（与R0内核保持一致）
    mov r10, 0E79086H           ; [理]
    mov r11, 0E5A198H           ; [塘]
    mov r12, 0E78E8BH           ; [王]

    ; 参数已经在正确的寄存器中：
    ; RCX = VmcallNumber (Hypercall代码)
    ; RDX = OptionalParam1 (HYPERCALL_KEY - 密钥验证)
    ; R8  = OptionalParam2 (参数1)
    ; R9  = OptionalParam3 (参数2)

    ; 执行 VMCALL
    vmcall

    ; 恢复寄存器
    pop r12
    pop r11
    pop r10
    popfq

    ; 返回值在 RAX 中
    ret
VmxVmCall endp

end
