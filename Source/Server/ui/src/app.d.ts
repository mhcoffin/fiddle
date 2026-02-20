interface Window {
    __JUCE__?: {
        backend?: {
            signalReady: () => void;
            nativeLog: (msg: string) => void;
            requestSetupData: () => void;
            saveSelectedInstruments: (json: string) => void;
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
    setServerVersion: (ver: string) => void;
    setConnectionState: (connected: boolean) => void;
    setChannelInstrument: (channel: number, name: string) => void;
    setDoricoInstruments: (json: string) => void;
    setSelectedInstruments: (json: string) => void;
    setSaveResult: (result: string) => void;
    nativeLog: (msg: string) => void;
}

declare module "*.svelte" {
    import { type Component } from "svelte";
    const component: Component<any, any, any>;
    export default component;
}


