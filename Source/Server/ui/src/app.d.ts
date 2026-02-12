interface Window {
    __JUCE__?: {
        backend?: {
            signalReady: () => void;
            nativeLog: (msg: string) => void;
            [key: string]: any;
        };
    };
    juce?: {
        [key: string]: any;
    };
    __juce__?: {
        [key: string]: any;
    };
    addLogMessage: (msg: string, isError?: boolean) => void;
    updateNoteState: (noteData: any, status: string) => void;
    pushMidiEvent: (event: any) => void;
    setHeartbeat: (val: number) => void;
    nativeLog: (msg: string) => void;
}

declare module "*.svelte" {
    import { type Component } from "svelte";
    const component: Component<any, any, any>;
    export default component;
}


